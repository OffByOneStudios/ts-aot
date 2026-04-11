#!/usr/bin/env python3
"""test262 smoke harness — fast delta-check across representative sub-corpora.

Runs four targeted 500-test samples and prints a pass-rate table. Designed
for before/after comparison on a single compiler change: each run takes
roughly 5-15 minutes depending on the pass rate.

Usage:
    python tests/test262/smoke.py                 # run smoke suite
    python tests/test262/smoke.py --save          # record numbers as baseline
    python tests/test262/smoke.py --compare       # diff against baseline
    python tests/test262/smoke.py --limit 200     # smaller per-corpus sample

A single compiler change's effect should be visible as a delta on one of the
four corpora below. If every corpus moves by <1% after a fix that was
expected to be high-leverage, that's the "off by 5-100x" smell described in
runtime_structural_issues.md — stop and investigate.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

SCRIPT_DIR = Path(__file__).parent
RUNNER = SCRIPT_DIR / "run_test262.py"
BASELINE_FILE = SCRIPT_DIR / ".smoke_baseline.json"

# Representative sub-corpora. Each is the --category argument to run_test262.py.
# Ordered so the fastest/most-critical corpus runs first.
CORPORA = [
    ("language/expressions",  "lang/expr"),
    ("language/statements",   "lang/stmt"),
    ("built-ins/Object",      "Object"),
    ("built-ins/Array",       "Array"),
]


@dataclass
class CorpusResult:
    name: str
    passed: int = 0
    failed: int = 0
    compile_error: int = 0
    timeout: int = 0
    crash: int = 0
    skipped: int = 0
    total_run: int = 0
    elapsed_s: float = 0.0

    @property
    def pass_rate(self) -> float:
        if self.total_run == 0:
            return 0.0
        return self.passed / self.total_run * 100.0

    def to_dict(self) -> dict:
        return {
            "passed": self.passed,
            "failed": self.failed,
            "compile_error": self.compile_error,
            "timeout": self.timeout,
            "crash": self.crash,
            "skipped": self.skipped,
            "total_run": self.total_run,
            "pass_rate": round(self.pass_rate, 2),
        }


# Regex patterns matching run_test262.py's summary output.
_SUMMARY_PATTERNS = {
    "passed":        re.compile(r"Passed:\s+(\d+)"),
    "failed":        re.compile(r"Failed:\s+(\d+)"),
    "compile_error": re.compile(r"Compile Error:\s+(\d+)"),
    "timeout":       re.compile(r"Timeout:\s+(\d+)"),
    "crash":         re.compile(r"Crash:\s+(\d+)"),
    "skipped":       re.compile(r"Skipped:\s+(\d+)"),
    "total_run":     re.compile(r"Total Run:\s+(\d+)"),
}
_ELAPSED_PATTERN = re.compile(r"test262 Results \(([\d.]+)s\)")


def parse_summary(stdout: str, name: str) -> CorpusResult:
    """Extract the summary block printed by run_test262.py."""
    result = CorpusResult(name=name)
    for field, pat in _SUMMARY_PATTERNS.items():
        m = pat.search(stdout)
        if m:
            setattr(result, field, int(m.group(1)))
    m = _ELAPSED_PATTERN.search(stdout)
    if m:
        result.elapsed_s = float(m.group(1))
    return result


def run_corpus(category: str, limit: int, jobs: int,
               timeout: int, verbose: bool) -> CorpusResult:
    """Invoke run_test262.py for a single category and capture its summary."""
    cmd = [
        sys.executable, str(RUNNER),
        "--category", category,
        "--limit", str(limit),
        "--jobs", str(jobs),
        "--timeout", str(timeout),
        "--interleave",
    ]
    if verbose:
        print(f"\n$ {' '.join(cmd)}", flush=True)

    start = time.time()
    proc = subprocess.run(
        cmd, capture_output=True, text=True,
        encoding="utf-8", errors="replace",
    )
    elapsed = time.time() - start

    if verbose:
        # Echo the runner's output so the operator sees progress
        print(proc.stdout, end="")
        if proc.stderr:
            print(proc.stderr, end="", file=sys.stderr)

    name = category  # Use the full category as the corpus name
    result = parse_summary(proc.stdout, name)
    if result.elapsed_s == 0.0:
        result.elapsed_s = elapsed
    return result


def print_table(results: List[CorpusResult], label: str = "") -> None:
    """Print a pass-rate table for the smoke results."""
    if label:
        print(f"\n{label}")
    print("=" * 78)
    print(f"  {'corpus':<24}  {'pass':>6}  {'fail':>6}  {'cerr':>6}  "
          f"{'skip':>6}  {'run':>6}  {'rate':>7}  {'time':>7}")
    print("-" * 78)
    total_pass = total_run = 0
    for r in results:
        print(f"  {r.name:<24}  {r.passed:>6}  {r.failed:>6}  "
              f"{r.compile_error:>6}  {r.skipped:>6}  {r.total_run:>6}  "
              f"{r.pass_rate:>6.1f}%  {r.elapsed_s:>6.0f}s")
        total_pass += r.passed
        total_run += r.total_run
    print("-" * 78)
    overall = (total_pass / total_run * 100.0) if total_run else 0.0
    print(f"  {'OVERALL':<24}  {total_pass:>6}  {'':>6}  {'':>6}  "
          f"{'':>6}  {total_run:>6}  {overall:>6.1f}%")
    print("=" * 78)


def save_baseline(results: List[CorpusResult]) -> None:
    data = {r.name: r.to_dict() for r in results}
    BASELINE_FILE.write_text(json.dumps(data, indent=2, sort_keys=True))
    print(f"\nSaved baseline: {BASELINE_FILE}")


def load_baseline() -> Optional[Dict[str, dict]]:
    if not BASELINE_FILE.exists():
        return None
    try:
        return json.loads(BASELINE_FILE.read_text())
    except Exception as e:
        print(f"Warning: failed to load baseline: {e}", file=sys.stderr)
        return None


def print_delta(results: List[CorpusResult], baseline: Dict[str, dict]) -> None:
    """Print delta between current results and baseline."""
    print("\nDelta vs baseline")
    # (note: emoji / arrows deliberately avoided — Windows cp1252 console chokes)
    print("=" * 78)
    print(f"  {'corpus':<24}  {'dpass':>10}  {'drate':>10}  {'drun':>10}")
    print("-" * 78)
    any_change = False
    for r in results:
        base = baseline.get(r.name)
        if not base:
            print(f"  {r.name:<24}  {'(new)':>10}")
            continue
        pass_d = r.passed - base.get("passed", 0)
        rate_d = r.pass_rate - base.get("pass_rate", 0.0)
        run_d = r.total_run - base.get("total_run", 0)
        marker = ""
        if pass_d > 0:
            marker = " +"
        elif pass_d < 0:
            marker = " !"
        if pass_d != 0 or abs(rate_d) > 0.05:
            any_change = True
        print(f"  {r.name:<24}  {pass_d:>+10}  {rate_d:>+9.2f}%  "
              f"{run_d:>+10}{marker}")
    print("=" * 78)
    if not any_change:
        print("  (no changes)")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--limit", type=int, default=500,
                    help="Max tests per corpus (default: 500)")
    ap.add_argument("--jobs", "-j", type=int, default=12,
                    help="Parallel jobs per corpus (default: 12)")
    ap.add_argument("--timeout", "-t", type=int, default=8,
                    help="Per-test execution timeout in seconds (default: 8)")
    ap.add_argument("--save", action="store_true",
                    help="Save results as smoke baseline")
    ap.add_argument("--compare", action="store_true",
                    help="Compare against saved baseline (default if baseline exists)")
    ap.add_argument("--verbose", "-v", action="store_true",
                    help="Echo run_test262.py output")
    args = ap.parse_args()

    if not RUNNER.exists():
        print(f"Error: runner not found at {RUNNER}", file=sys.stderr)
        return 1

    baseline = load_baseline()
    if args.compare and baseline is None:
        print("Error: no baseline to compare against. Run with --save first.",
              file=sys.stderr)
        return 1

    print(f"Smoke run: {len(CORPORA)} corpora x {args.limit} tests each, "
          f"-j {args.jobs}, timeout {args.timeout}s")
    start = time.time()
    results: List[CorpusResult] = []

    for idx, (category, _short) in enumerate(CORPORA, 1):
        print(f"\n[{idx}/{len(CORPORA)}] {category} ...", flush=True)
        r = run_corpus(category, args.limit, args.jobs, args.timeout, args.verbose)
        results.append(r)
        print(f"  -> pass {r.passed}/{r.total_run} ({r.pass_rate:.1f}%), "
              f"fail {r.failed}, cerr {r.compile_error}, skip {r.skipped}, "
              f"{r.elapsed_s:.0f}s", flush=True)

    total_elapsed = time.time() - start
    print_table(results, label=f"Smoke results (total {total_elapsed:.0f}s)")

    if baseline:
        print_delta(results, baseline)

    if args.save:
        save_baseline(results)

    return 0


if __name__ == "__main__":
    sys.exit(main())
