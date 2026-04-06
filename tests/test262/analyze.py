#!/usr/bin/env python3
"""Analyze test262 results from .test262_results.jsonl.

Usage:
    python tests/test262/analyze.py              # Summary by top-level category
    python tests/test262/analyze.py --depth 3    # Group by 3 path levels
    python tests/test262/analyze.py --sort gap   # Sort by tests-to-gain (default)
    python tests/test262/analyze.py --sort rate  # Sort by pass rate
    python tests/test262/analyze.py --min 10     # Only show categories with >= 10 tests
"""

import argparse
import json
from collections import defaultdict
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
RESULTS_JSONL = SCRIPT_DIR / ".test262_results.jsonl"


def load_results():
    """Load all results from JSONL, deduplicating by path (last wins)."""
    results = {}
    if not RESULTS_JSONL.exists():
        print(f"Error: {RESULTS_JSONL} not found")
        return results
    with open(RESULTS_JSONL, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
                p = rec.get("path", "").replace("\\", "/")
                s = rec.get("status")
                if p and s:
                    results[p] = s
            except json.JSONDecodeError:
                continue
    return results


def category_key(path: str, depth: int) -> str:
    """Get the first `depth` path components as a category key."""
    parts = path.split("/")
    return "/".join(parts[:depth])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--depth", type=int, default=2,
                        help="Path depth for grouping (default: 2)")
    parser.add_argument("--sort", choices=["gap", "rate", "total", "pass"],
                        default="gap")
    parser.add_argument("--min", type=int, default=5,
                        help="Minimum total tests per category")
    parser.add_argument("--include-skips", action="store_true",
                        help="Include skipped tests in totals")
    parser.add_argument("--top", type=int, default=40,
                        help="Show top N categories")
    args = parser.parse_args()

    results = load_results()
    if not results:
        return

    # Aggregate by category
    counts = defaultdict(lambda: {"pass": 0, "fail": 0, "skip": 0,
                                  "compile_error": 0, "timeout": 0,
                                  "crash": 0, "total": 0})
    for path, status in results.items():
        cat = category_key(path, args.depth)
        counts[cat][status] = counts[cat].get(status, 0) + 1
        if status != "skip" or args.include_skips:
            counts[cat]["total"] += 1

    # Build rows
    rows = []
    total_pass = 0
    total_run = 0
    for cat, d in counts.items():
        total = d["total"]
        if total < args.min:
            continue
        p = d["pass"]
        f = d["fail"] + d["compile_error"] + d["timeout"] + d["crash"]
        rate = (p / total * 100) if total else 0
        gap = total - p
        rows.append({
            "cat": cat,
            "pass": p,
            "fail": f,
            "total": total,
            "rate": rate,
            "gap": gap,
            "compile_error": d["compile_error"],
            "timeout": d["timeout"],
        })
        total_pass += p
        total_run += total

    # Sort
    if args.sort == "gap":
        rows.sort(key=lambda r: -r["gap"])
    elif args.sort == "rate":
        rows.sort(key=lambda r: r["rate"])
    elif args.sort == "total":
        rows.sort(key=lambda r: -r["total"])
    elif args.sort == "pass":
        rows.sort(key=lambda r: -r["pass"])

    # Print overall
    total_skipped = sum(d["skip"] for d in counts.values())
    overall_rate = (total_pass / total_run * 100) if total_run else 0
    print()
    print("=" * 78)
    print(f"test262 Results — {len(results)} total, {total_run} run "
          f"({total_skipped} skipped)")
    print(f"  Overall: {total_pass}/{total_run} = {overall_rate:.1f}% pass rate")
    print("=" * 78)
    print()

    # Header
    print(f"{'Category':<45} {'Pass':>5} {'Fail':>5} {'Total':>6} "
          f"{'Rate':>6} {'Gap':>6}")
    print("-" * 78)
    for r in rows[: args.top]:
        cat = r["cat"]
        if len(cat) > 44:
            cat = "..." + cat[-41:]
        print(f"{cat:<45} {r['pass']:>5} {r['fail']:>5} {r['total']:>6} "
              f"{r['rate']:>5.1f}% {r['gap']:>6}")

    if len(rows) > args.top:
        print(f"... ({len(rows) - args.top} more rows)")
    print()


if __name__ == "__main__":
    main()
