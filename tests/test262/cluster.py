#!/usr/bin/env python3
"""Cluster test262 failures by their assertion/error message.

Helps identify the biggest single-pattern failure clusters so we know
what fix will move the most tests. Groups by a normalized prefix of the
`reason` field captured in .test262_results.jsonl.

Usage:
    python tests/test262/cluster.py                   # Top 30 clusters by size
    python tests/test262/cluster.py --top 60          # More rows
    python tests/test262/cluster.py --filter Date     # Only clusters matching "Date"
    python tests/test262/cluster.py --list testResult # Show test paths in matching clusters
    python tests/test262/cluster.py --prefix 80       # Cluster on first N chars of reason
"""

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
RESULTS_JSONL = SCRIPT_DIR / ".test262_results.jsonl"


def normalize_reason(reason: str, prefix_len: int) -> str:
    """Strip volatile bits from a reason string so similar failures group.

    Test262 harness wraps most failures in "Uncaught: <assertion> Expected ..."
    — we keep the assertion part and drop test-specific names like variable
    values, line numbers, and object identities.
    """
    if not reason:
        return "<no-reason>"
    # Drop leading "Uncaught: " / "Test262Error: " prefixes
    s = re.sub(r"^(Uncaught:?\s*|Test262Error:\s*)", "", reason)
    # Collapse specific values: numbers, quoted strings, `[object X]`
    s = re.sub(r"\[object [^\]]+\]", "[object ?]", s)
    s = re.sub(r"«[^»]*»", "«?»", s)             # test262 SameValue wrappers
    s = re.sub(r"\b-?\d+(\.\d+)?\b", "N", s)     # numbers → N
    s = re.sub(r'"[^"]*"', '"?"', s)             # quoted strings
    s = re.sub(r"'[^']*'", "'?'", s)
    s = re.sub(r"\s+", " ", s).strip()
    return s[:prefix_len] if prefix_len else s


def load_failures():
    """Yield (path, reason) for each failing test."""
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
            if rec.get("status") != "fail":
                continue
            yield rec.get("path", ""), rec.get("reason", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=30,
                    help="Show top N clusters (default 30)")
    ap.add_argument("--filter", default="",
                    help="Only show clusters containing this substring")
    ap.add_argument("--list", default="",
                    help="Print test paths in clusters matching this substring")
    ap.add_argument("--prefix", type=int, default=120,
                    help="Cluster on first N chars of normalized reason (default 120)")
    ap.add_argument("--min", type=int, default=2,
                    help="Suppress clusters with fewer than N members (default 2)")
    args = ap.parse_args()

    clusters = defaultdict(list)
    total_fails = 0
    for path, reason in load_failures():
        total_fails += 1
        key = normalize_reason(reason, args.prefix)
        clusters[key].append(path)

    rows = sorted(clusters.items(), key=lambda kv: -len(kv[1]))

    if args.list:
        needle = args.list.lower()
        for key, paths in rows:
            if needle not in key.lower():
                continue
            print(f"\n=== {len(paths)}  {key} ===")
            for p in paths[:50]:
                print(f"  {p}")
            if len(paths) > 50:
                print(f"  ... ({len(paths) - 50} more)")
        return

    needle = args.filter.lower()
    print()
    print("=" * 78)
    print(f"test262 failure clusters  ({total_fails} total fails)")
    print("=" * 78)
    print(f"{'Count':>6}  {'Example paths / cluster reason'}")
    print("-" * 78)

    shown = 0
    for key, paths in rows:
        if len(paths) < args.min:
            break
        if needle and needle not in key.lower():
            continue
        if shown >= args.top:
            break
        # Display: count, one example path (basename), first 110 chars of key
        example = Path(paths[0]).stem if paths else ""
        line = f"{len(paths):>6}  {example[:35]:<35}  {key[:100]}"
        print(line)
        shown += 1

    print()
    top_sum = sum(len(p) for _, p in rows[:args.top])
    print(f"Top {min(args.top, len(rows))} clusters cover {top_sum}/"
          f"{total_fails} fails ({100*top_sum/max(1,total_fails):.1f}%)")


if __name__ == "__main__":
    main()
