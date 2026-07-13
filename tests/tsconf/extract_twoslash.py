#!/usr/bin/env python3
"""TSCONF-001 Phase 0: twoslash block extractor for the TypeScript-Website
corpus (handbook + reference pages). Produces tests/tsconf/twoslash.json —
one record per ```ts twoslash``` code block:

    { "doc": relpath, "index": n-within-doc, "code": str,
      "expected_errors": [codes...], "type_queries": count,
      "options": {k: v} }

Test kinds derived downstream (Phase 3):
- positive (no @errors, no ^?): must compile
- negative (@errors: NNNN): expected diagnostic codes — the curated neg corpus
- type-assertion (^?): compare --dump-types at the position (aspirational)

Usage:
    python tests/tsconf/extract_twoslash.py tests/tsconf/upstream/TypeScript-Website
"""
import json
import os
import re
import sys

FENCE_RE = re.compile(r"^```(\w+)\s+twoslash\s*$")
META_RE = re.compile(r"^//\s*@(\w+)(?::\s*(.*?))?\s*$")
QUERY_RE = re.compile(r"^\s*//\s*\^\?")


def extract(root):
    docroot = os.path.join(root, "packages", "documentation", "copy", "en")
    if not os.path.isdir(docroot):
        docroot = root  # fall back to scanning whatever was vendored
    blocks = []
    for dirpath, _dirs, files in os.walk(docroot):
        for fn in files:
            if not fn.endswith(".md"):
                continue
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, root).replace("\\", "/")
            with open(p, encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()
            in_block = False
            lang = None
            body = []
            idx = 0
            for line in lines:
                if not in_block:
                    m = FENCE_RE.match(line.strip())
                    if m:
                        in_block = True
                        lang = m.group(1)
                        body = []
                    continue
                if line.strip() == "```":
                    in_block = False
                    code = "\n".join(body)
                    errors = []
                    options = {}
                    queries = 0
                    for bl in body:
                        qm = QUERY_RE.match(bl)
                        if qm:
                            queries += 1
                            continue
                        mm = META_RE.match(bl.strip())
                        if mm:
                            k = mm.group(1).lower()
                            v = (mm.group(2) or "").strip()
                            if k == "errors":
                                errors += [e for e in re.split(r"[ ,]+", v) if e.isdigit()]
                            else:
                                options[k] = v
                    blocks.append({
                        "doc": rel, "index": idx, "lang": lang,
                        "code": code, "expected_errors": errors,
                        "type_queries": queries, "options": options,
                    })
                    idx += 1
                    continue
                body.append(line)
    return blocks


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "tests/tsconf/upstream/TypeScript-Website"
    blocks = extract(root)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "twoslash.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(blocks, f, indent=0)
    neg = sum(1 for b in blocks if b["expected_errors"])
    ty = sum(1 for b in blocks if b["type_queries"])
    pos = sum(1 for b in blocks if not b["expected_errors"] and not b["type_queries"])
    ts_only = sum(1 for b in blocks if b["lang"] in ("ts", "tsx", "typescript"))
    print(f"blocks: {len(blocks)} (lang ts/tsx: {ts_only})")
    print(f"  positive (must-compile): {pos}")
    print(f"  negative (@errors):      {neg}")
    print(f"  type-assertion (^?):     {ty}")
    print(f"written: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
