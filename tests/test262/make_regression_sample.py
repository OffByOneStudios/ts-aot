#!/usr/bin/env python3
"""Generate the stratified regression sample (tests/test262/regression_sample.txt).

Selects ~2k tests from the latest full-sweep results, stratified over the
hot areas where past regressions concentrated (Array.prototype iteration,
property descriptors, classes/destructuring, property access), weighted
~90% currently-passing / ~10% currently-failing per stratum. Selection is
deterministic (md5 ordering), so re-running against the same sweep yields
the same sample.

Usage: python make_regression_sample.py [results_jsonl] [out_txt]
"""
import hashlib
import json
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
RESULTS = Path(sys.argv[1]) if len(sys.argv) > 1 else SCRIPT_DIR / ".test262_results.jsonl"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else SCRIPT_DIR / "regression_sample.txt"

# (path-prefix, quota) — prefixes use '/' separators
STRATA = [
    ("built-ins/Array/prototype/", 700),
    ("built-ins/TypedArray/", 100),
    ("built-ins/TypedArrayConstructors/", 50),
    ("built-ins/Object/defineProperty/", 120),
    ("built-ins/Object/defineProperties/", 60),
    ("built-ins/Object/getOwnPropertyDescriptor/", 40),
    ("built-ins/Object/create/", 40),
    ("built-ins/Object/keys/", 30),
    ("built-ins/Object/", 60),           # remaining Object built-ins
    ("language/statements/class/", 250),
    ("language/expressions/class/", 100),
    ("language/statements/for-in/", 50),
    ("language/expressions/assignment/", 100),
    ("language/expressions/object/", 100),
    ("language/expressions/in/", 30),
    ("language/expressions/delete/", 30),
    ("language/expressions/", 140),      # remaining expressions (property access etc.)
]
PASS_FRACTION = 0.9


def main():
    if not RESULTS.exists():
        sys.exit(f"results file not found: {RESULTS}")

    latest = {}
    with RESULTS.open(encoding="utf-8") as f:
        for line in f:
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            latest[rec["path"].replace("\\", "/")] = rec["status"]

    taken = set()
    sample = []
    for prefix, quota in STRATA:
        passing = sorted(
            (p for p, s in latest.items()
             if p.startswith(prefix) and s == "pass" and p not in taken),
            key=lambda p: hashlib.md5(p.encode()).hexdigest())
        failing = sorted(
            (p for p, s in latest.items()
             if p.startswith(prefix) and s in ("fail", "timeout") and p not in taken),
            key=lambda p: hashlib.md5(p.encode()).hexdigest())
        n_pass = min(int(quota * PASS_FRACTION), len(passing))
        n_fail = min(quota - n_pass, len(failing))
        chosen = passing[:n_pass] + failing[:n_fail]
        taken.update(chosen)
        sample.extend(chosen)
        print(f"{prefix:50s} {len(chosen):4d} ({n_pass} pass / {n_fail} fail)")

    sample.sort()
    OUT.write_text("\n".join(sample) + "\n", encoding="utf-8")
    n_pass_total = sum(1 for p in sample if latest[p] == "pass")
    print(f"\nwrote {len(sample)} tests ({n_pass_total} pass / "
          f"{len(sample) - n_pass_total} fail/timeout) -> {OUT}")


if __name__ == "__main__":
    main()
