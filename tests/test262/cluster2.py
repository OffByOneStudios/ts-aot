#!/usr/bin/env python3
"""Cluster test262 failures by (path-prefix, normalized-reason) compound key.

The original cluster.py groups by reason alone, which produces misleadingly
broad clusters like "Expected SameValue(?, ?) to be true" that span every
spec area. This tool groups by (spec_area_path_prefix, reason_fingerprint)
so each cluster genuinely shares a root cause.

The path prefix depth is configurable (default 3): for a test at
`built-ins/BigInt/prototype/toString/argument-symbol.js`, depth=3 yields
`built-ins/BigInt/prototype` — the spec sub-area.

Reason fingerprint normalizes:
  - Numbers, quoted strings, [object X] wrappers
  - test262 SameValue «...» operands
  - File paths, line/col numbers
And truncates to ~60 chars so similar messages collapse.

Usage:
    python tests/test262/cluster2.py                # Top 30 clusters
    python tests/test262/cluster2.py --depth 4      # Deeper spec area split
    python tests/test262/cluster2.py --area BigInt  # Only clusters touching BigInt
    python tests/test262/cluster2.py --reason CRASH # Only clusters with CRASH reasons
    python tests/test262/cluster2.py --status compile_error  # Only ce'd tests
    python tests/test262/cluster2.py --list KEY     # Show test paths in clusters matching KEY
"""

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
RESULTS_JSONL = SCRIPT_DIR / ".test262_results.jsonl"


def normalize_reason(reason: str, prefix_len: int = 60) -> str:
    """Strip volatile bits to produce a stable cluster fingerprint."""
    if not reason:
        return "<no-reason>"
    s = re.sub(r"^(Uncaught:?\s*|Test262Error:\s*)", "", reason)
    s = re.sub(r"\[object [^\]]+\]", "[object?]", s)
    s = re.sub(r"«[^»]*»", "«?»", s)
    s = re.sub(r"\b-?\d+(\.\d+)?(e[+-]?\d+)?\b", "N", s, flags=re.IGNORECASE)
    s = re.sub(r"0x[0-9a-fA-F]+", "0x?", s)
    s = re.sub(r'"[^"]*"', '"?"', s)
    s = re.sub(r"'[^']*'", "'?'", s)
    s = re.sub(r"[A-Za-z]:[\\/][^\s'\"]+", "<path>", s)  # absolute paths
    s = re.sub(r"\s+", " ", s).strip()
    # Bucket common categorical signatures
    if "VectoredException" in s or "0xc?" in s:
        return "CRASH access violation"
    if "Expected SameValue" in s:
        return "SameValue mismatch"
    if "Expected a TypeError" in s:
        return "missing TypeError"
    if "Expected a RangeError" in s:
        return "missing RangeError"
    if "Expected a SyntaxError" in s:
        return "missing SyntaxError"
    if "Expected a ReferenceError" in s:
        return "missing ReferenceError"
    if "Expected a Test?Error" in s or "Expected a TestNError" in s:
        return "expected Test262Error not thrown"
    if "Expected true but got false" in s:
        return "expected true got false"
    if "Expected false but got true" in s:
        return "expected false got true"
    if "compilation failed" in s:
        return "compilation failed"
    if "expected parse error" in s:
        return "expected parse error"
    if "execution timeout" in s:
        return "execution timeout"
    if "undefined symbol" in s:
        m = re.search(r"undefined symbol: (\S+)", s)
        return f"linker undefined symbol: {m.group(1) if m else '?'}"
    if "should have an own property" in s:
        m = re.search(r"own property (\S+)", s)
        return f"missing own property {m.group(1) if m else '?'}"
    if "exception (no message)" in s:
        return "uncaught exception (no message)"
    return s[:prefix_len]


def path_prefix(path: str, depth: int) -> str:
    """Return the first N path components, normalized to forward slashes."""
    parts = re.split(r"[\\/]", path)
    return "/".join(parts[:depth])


def load_records(status_filter: str = ""):
    """Yield (path, status, reason) for each result row."""
    if not RESULTS_JSONL.exists():
        print(f"Error: {RESULTS_JSONL} not found. Run tests first.")
        return
    with open(RESULTS_JSONL, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            status = rec.get("status", "")
            if status_filter:
                if status_filter == "fail" and status not in ("fail", "compile_error", "timeout"):
                    continue
                if status_filter != "fail" and status != status_filter:
                    continue
            else:
                if status not in ("fail", "compile_error", "timeout"):
                    continue
            yield rec.get("path", ""), status, rec.get("reason", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--depth", type=int, default=3,
                    help="Path-prefix depth (default 3 — e.g., built-ins/BigInt/prototype)")
    ap.add_argument("--top", type=int, default=30, help="Show top N clusters")
    ap.add_argument("--area", default="",
                    help="Filter: only show clusters whose path-prefix matches")
    ap.add_argument("--reason", default="",
                    help="Filter: only show clusters whose reason fingerprint matches")
    ap.add_argument("--status", default="",
                    help="Filter by status: fail / compile_error / timeout")
    ap.add_argument("--list", default="",
                    help="Print test paths in clusters matching KEY (substring of either axis)")
    ap.add_argument("--min", type=int, default=2,
                    help="Suppress clusters with fewer than N members")
    args = ap.parse_args()

    clusters = defaultdict(list)
    total_records = 0
    for path, status, reason in load_records(args.status):
        total_records += 1
        prefix = path_prefix(path, args.depth)
        sig = normalize_reason(reason)
        # Tag compile_error and timeout in the reason key so they don't get
        # mixed with runtime fails sharing a generic SameValue signature.
        if status == "compile_error":
            sig = f"[compile_error] {sig}"
        elif status == "timeout":
            sig = "[timeout]"
        clusters[(prefix, sig)].append(path)

    rows = sorted(clusters.items(), key=lambda kv: -len(kv[1]))

    if args.list:
        needle = args.list.lower()
        for (prefix, sig), paths in rows:
            if needle not in prefix.lower() and needle not in sig.lower():
                continue
            print(f"\n=== {len(paths):4}  {prefix}  |  {sig}")
            for p in paths[:30]:
                print(f"  {p}")
            if len(paths) > 30:
                print(f"  ... ({len(paths) - 30} more)")
        return

    area_needle = args.area.lower()
    reason_needle = args.reason.lower()

    print()
    print("=" * 96)
    print(f"test262 clusters  ({total_records} total {args.status or 'failing'} results, "
          f"depth={args.depth})")
    print("=" * 96)
    print(f"{'Count':>5}  {'Spec area':<38}  {'Failure shape':<48}")
    print("-" * 96)
    shown = 0
    for (prefix, sig), paths in rows:
        if len(paths) < args.min:
            break
        if area_needle and area_needle not in prefix.lower():
            continue
        if reason_needle and reason_needle not in sig.lower():
            continue
        if shown >= args.top:
            break
        print(f"{len(paths):>5}  {prefix[:38]:<38}  {sig[:48]:<48}")
        shown += 1
    print()
    top_sum = sum(len(p) for _, p in rows[:args.top])
    print(f"Top {min(args.top, len(rows))} clusters cover {top_sum}/"
          f"{total_records} ({100*top_sum/max(1,total_records):.1f}%)")


if __name__ == "__main__":
    main()
