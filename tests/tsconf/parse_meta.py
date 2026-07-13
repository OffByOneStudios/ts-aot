#!/usr/bin/env python3
"""TSCONF-001 Phase 0: TypeScript test-suite metadata parser + corpus census.

Parses the inline `// @option: value` headers used by microsoft/TypeScript's
test runner (tests/cases/conformance/**), including `@filename:` multi-file
splitting, and produces a per-directory census of options so skip-lists are
DERIVED FROM DATA with counted reasons (never silent).

Library use (Phase 1 runner imports this):
    from parse_meta import parse_test
    t = parse_test(path)   # -> TestCase(options, files, is_multifile, variants)

CLI census:
    python tests/tsconf/parse_meta.py census tests/tsconf/upstream/TypeScript/tests/cases/conformance
"""
import collections
import json
import os
import re
import sys
from dataclasses import dataclass, field

# TypeScript's own harness matches option lines with this shape (whitespace
# tolerant, option names case-insensitive). A line only counts as metadata in
# the header region or immediately between virtual files; the upstream runner
# treats any `// @x: y` line anywhere as metadata, so we do the same.
META_RE = re.compile(r"^\s*//\s*@(\w+)\s*:\s*(.*?)\s*$")

# Options whose comma-separated values fan out into N test VARIANTS in the
# upstream runner (each value = one compilation).
VARIANT_OPTIONS = {"target", "module", "moduleresolution", "strict"}


@dataclass
class VirtualFile:
    name: str
    content: str = ""


@dataclass
class TestCase:
    path: str
    options: dict = field(default_factory=dict)   # lowercased key -> raw value
    files: list = field(default_factory=list)     # [VirtualFile]; [0] may be the implicit main
    is_multifile: bool = False

    def variant_count(self):
        n = 1
        for k in VARIANT_OPTIONS:
            v = self.options.get(k, "")
            if "," in v:
                n *= len([p for p in v.split(",") if p.strip()])
        return n


def parse_test(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    tc = TestCase(path=path)
    cur = VirtualFile(name=os.path.basename(path))
    saw_named = False
    lines = text.splitlines(keepends=True)
    body = []
    for line in lines:
        m = META_RE.match(line)
        if m:
            key = m.group(1).lower()
            val = m.group(2)
            if key == "filename":
                # flush current file, start a new one
                cur.content = "".join(body)
                if saw_named or cur.content.strip():
                    tc.files.append(cur)
                cur = VirtualFile(name=val.strip())
                body = []
                saw_named = True
                continue
            # global (or per-file, rare) compiler option
            if key not in tc.options:
                tc.options[key] = val
            else:
                tc.options[key] += "," + val
            continue
        body.append(line)
    cur.content = "".join(body)
    tc.files.append(cur)
    tc.is_multifile = saw_named
    return tc


def census(root):
    per_dir = collections.defaultdict(lambda: {
        "tests": 0, "multifile": 0, "tsx": 0, "variants_gt1": 0,
        "options": collections.Counter(),
    })
    global_opts = collections.Counter()
    opt_examples = {}
    total = 0
    for dirpath, _dirs, files in os.walk(root):
        for fn in files:
            if not fn.endswith((".ts", ".tsx")):
                continue
            total += 1
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(dirpath, root).replace("\\", "/")
            top = rel.split("/")[0] if rel != "." else "."
            d = per_dir[top]
            d["tests"] += 1
            if fn.endswith(".tsx"):
                d["tsx"] += 1
            try:
                tc = parse_test(p)
            except Exception as e:  # parse failure is itself census data
                d["options"]["<PARSE-ERROR>"] += 1
                global_opts["<PARSE-ERROR>"] += 1
                continue
            if tc.is_multifile:
                d["multifile"] += 1
            if tc.variant_count() > 1:
                d["variants_gt1"] += 1
            for k in tc.options:
                d["options"][k] += 1
                global_opts[k] += 1
                opt_examples.setdefault(k, os.path.relpath(p, root))
    return total, per_dir, global_opts, opt_examples


def main():
    if len(sys.argv) < 3 or sys.argv[1] != "census":
        print(__doc__)
        return 1
    root = sys.argv[2]
    total, per_dir, global_opts, opt_examples = census(root)
    out = {
        "total_tests": total,
        "global_option_frequency": dict(global_opts.most_common()),
        "option_example": opt_examples,
        "per_directory": {
            k: {
                "tests": v["tests"], "multifile": v["multifile"],
                "tsx": v["tsx"], "variants_gt1": v["variants_gt1"],
                "top_options": dict(v["options"].most_common(8)),
            } for k, v in sorted(per_dir.items())
        },
    }
    json_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "census.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    print(f"total tests: {total}")
    print(f"census written: {json_path}")
    print("\n== global option frequency (top 40) ==")
    for k, n in global_opts.most_common(40):
        print(f"{n:5d}  @{k:<28} e.g. {opt_examples.get(k,'')[:60]}")
    print("\n== per-directory (top 25 by tests) ==")
    rows = sorted(per_dir.items(), key=lambda kv: -kv[1]["tests"])[:25]
    print(f"{'dir':<26}{'tests':>6}{'multi':>7}{'tsx':>6}{'var>1':>7}")
    for k, v in rows:
        print(f"{k:<26}{v['tests']:>6}{v['multifile']:>7}{v['tsx']:>6}{v['variants_gt1']:>7}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
