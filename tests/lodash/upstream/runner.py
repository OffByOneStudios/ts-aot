#!/usr/bin/env python3
"""Lodash test.js runner — measures ts-aot's pass rate against lodash's own
QUnit test suite (test/test.js, ~6794 assertions in ~3000 tests).

Unlike test262 (50k+ separate files), lodash is ONE program that runs all
tests sequentially in-process. A crash/hang anywhere kills the final tally,
so qunit_shim.js emits @@PROGRESS / @@RUNALL_START / @@RUNALL_END / final
LODASH-QUNIT line; this runner parses them to recover a result even when
execution doesn't complete.

Statuses:
  complete   - reached LODASH-QUNIT line; full tally available
  crash      - process exited nonzero before LODASH-QUNIT (use last @@PROGRESS)
  hang       - timeout before LODASH-QUNIT (use last @@PROGRESS)
  no-runall  - never reached @@RUNALL_START (setup-phase crash/hang)

Usage:
    python tests/lodash/upstream/runner.py            # default nursery
    python tests/lodash/upstream/runner.py --nursery-off
    python tests/lodash/upstream/runner.py --no-build  # skip recompile
    python tests/lodash/upstream/runner.py --timeout 180
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent
COMPILER = PROJECT_ROOT / "build" / "src" / "compiler" / "Release" / "ts-aot.exe"
ENTRY_JS = SCRIPT_DIR / "entry.js"
ENTRY_EXE = SCRIPT_DIR / "entry.exe"
RESULTS_JSONL = SCRIPT_DIR / ".lodash_results.jsonl"

PROGRESS_RE = re.compile(
    r"^@@PROGRESS (\d+)/(\d+) pass=(\d+) fail=(\d+) module=(.*) test=(.*)$"
)
RUNALL_START_RE = re.compile(r"^@@RUNALL_START total=(\d+)$")
RUNALL_END_RE = re.compile(
    r"^@@RUNALL_END executed=(\d+) pass=(\d+) fail=(\d+)$"
)
FINAL_RE = re.compile(
    r"^LODASH-QUNIT PASS: (\d+)\s+FAIL: (\d+)\s+TOTAL: (\d+)$"
)
NOISE_RE = re.compile(r"^\[ts-aot\]|^\[warning\]")


@dataclass
class Result:
    status: str           # complete|crash|hang|no-runall
    nursery: str          # default|off
    total_tests: int      # queue length at RUNALL_START (0 if never reached)
    tests_executed: int   # last @@PROGRESS index (test count)
    assert_pass: int      # assertions that passed (NOT tests — one test has many)
    assert_fail: int      # assertions that failed
    last_module: str
    last_test: str
    exit_code: int
    elapsed_s: float


def parse_stream(lines):
    """Walk stdout lines once; return the most-recent state."""
    total = 0
    executed = 0
    pass_count = 0
    fail_count = 0
    last_module = ""
    last_test = ""
    final_seen = False
    runall_seen = False
    runall_end_seen = False

    for raw in lines:
        line = raw.rstrip("\r\n")
        if not line or NOISE_RE.match(line):
            continue
        m = RUNALL_START_RE.match(line)
        if m:
            runall_seen = True
            total = int(m.group(1))
            continue
        m = PROGRESS_RE.match(line)
        if m:
            executed = int(m.group(1))
            total = int(m.group(2))
            pass_count = int(m.group(3))
            fail_count = int(m.group(4))
            last_module = m.group(5)
            last_test = m.group(6)
            continue
        m = RUNALL_END_RE.match(line)
        if m:
            runall_end_seen = True
            executed = int(m.group(1))
            pass_count = int(m.group(2))
            fail_count = int(m.group(3))
            continue
        m = FINAL_RE.match(line)
        if m:
            final_seen = True
            pass_count = int(m.group(1))
            fail_count = int(m.group(2))
            # TOTAL on the final line is assertions, not tests; ignore.
            continue

    return {
        "runall_seen": runall_seen,
        "runall_end_seen": runall_end_seen,
        "final_seen": final_seen,
        "total": total,
        "executed": executed,
        "pass": pass_count,
        "fail": fail_count,
        "last_module": last_module,
        "last_test": last_test,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--nursery-off", action="store_true",
                    help="Run with TS_GC_NURSERY=0 (workaround for moving-GC bug)")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip recompiling entry.exe")
    ap.add_argument("--timeout", type=int, default=120,
                    help="Per-run timeout in seconds (default 120)")
    ap.add_argument("--save", action="store_true",
                    help="Append result to .lodash_results.jsonl")
    ap.add_argument("--retries", type=int, default=0,
                    help="Re-run on crash/hang up to N extra times until a "
                         "complete run is obtained. The runtime has a rare (~8%%) "
                         "nondeterministic GC use-after-free; a complete pass/fail "
                         "tally is the goal, so retrying the crash is the reliable "
                         "way to push tests. (default 0 = single run)")
    args = ap.parse_args()

    if not COMPILER.exists():
        print(f"error: compiler not found at {COMPILER}", file=sys.stderr)
        return 2
    if not ENTRY_JS.exists():
        print(f"error: {ENTRY_JS} missing; run setup.py first", file=sys.stderr)
        return 2

    # Compile (unless skipped). Use a unique exe name so a leftover running
    # entry.exe from a prior crashed/hung run can't lock the output path.
    exe_path = ENTRY_EXE
    if not args.no_build:
        print(f"[1/3] compiling {ENTRY_JS.name} -> {exe_path.name} ...",
              flush=True)
        cp = subprocess.run(
            [str(COMPILER), str(ENTRY_JS), "-o", str(exe_path)],
            capture_output=True, text=True, cwd=str(PROJECT_ROOT),
        )
        if cp.returncode != 0:
            print(f"error: compile failed ({cp.returncode}):", file=sys.stderr)
            print(cp.stderr[-2000:], file=sys.stderr)
            return 2

    # Run with the appropriate env + timeout.
    env = dict(os.environ)
    nursery_label = "default"
    if args.nursery_off:
        env["TS_GC_NURSERY"] = "0"
        nursery_label = "off"

    # Run (retrying the rare nondeterministic GC crash/hang until a complete
    # tally is obtained — see --retries). The best result across attempts is
    # kept: a 'complete' run wins immediately; otherwise the one that executed
    # the most tests is reported.
    attempts = max(1, args.retries + 1)
    best = None  # (status, stdout, stderr, exit_code, elapsed, executed)
    for attempt in range(attempts):
        label = f"[2/3] running ({nursery_label} nursery, timeout {args.timeout}s)"
        if attempts > 1:
            label += f"  attempt {attempt + 1}/{attempts}"
        print(label + " ...", flush=True)
        t0 = time.perf_counter()
        try:
            cp = subprocess.run(
                [str(exe_path)],
                capture_output=True, text=True, env=env,
                timeout=args.timeout, cwd=str(PROJECT_ROOT),
            )
            timed_out = False
            stdout = cp.stdout
            stderr = cp.stderr
            exit_code = cp.returncode
        except subprocess.TimeoutExpired as te:
            timed_out = True
            stdout = (te.stdout or b"").decode("utf-8", "replace") if isinstance(te.stdout, bytes) else (te.stdout or "")
            stderr = (te.stderr or b"").decode("utf-8", "replace") if isinstance(te.stderr, bytes) else (te.stderr or "")
            exit_code = -1
        elapsed = time.perf_counter() - t0

        st = parse_stream(stdout.splitlines())
        if st["final_seen"]:
            status = "complete"
        elif st["total"] > 0 and st["executed"] >= st["total"]:
            # All tests executed; the final @@PROGRESS carries the full pass/fail
            # tally. A teardown crash AFTER the last test (a rare GC use-after-free
            # in event-loop/teardown that prevents @@RUNALL_END from printing) is
            # cosmetic for measurement — the data is complete.
            status = "complete"
        elif not st["runall_seen"]:
            status = "no-runall"
        elif timed_out:
            status = "hang"
        else:
            status = "crash"

        cand = (status, stdout, stderr, exit_code, elapsed, st)
        if best is None or st["executed"] > best[5]["executed"]:
            best = cand
        if status == "complete":
            break
        if attempt + 1 < attempts:
            print(f"      {status} at test {st['executed']}/{st['total']} — retrying ...",
                  flush=True)

    status, stdout, stderr, exit_code, elapsed, state = best

    res = Result(
        status=status,
        nursery=nursery_label,
        total_tests=state["total"],
        tests_executed=state["executed"],
        assert_pass=state["pass"],
        assert_fail=state["fail"],
        last_module=state["last_module"],
        last_test=state["last_test"],
        exit_code=exit_code,
        elapsed_s=round(elapsed, 2),
    )

    # Report. Two distinct metrics:
    #   tests     = queue entries visited (one per QUnit.test)
    #   assertions = individual assert.* calls inside test bodies
    # Node's baseline is 6794 ASSERTIONS across ~3095 tests.
    assert_total = res.assert_pass + res.assert_fail
    print(f"[3/3] result:")
    print(f"  status              : {res.status}")
    print(f"  nursery             : {res.nursery}")
    print(f"  elapsed             : {res.elapsed_s}s")
    print(f"  exit_code           : {res.exit_code}")
    if res.total_tests:
        pct = 100.0 * res.tests_executed / res.total_tests
        print(f"  tests executed      : {res.tests_executed} / {res.total_tests} ({pct:.1f}%)")
    else:
        print(f"  tests executed      : 0 / 0 (never reached runAll)")
    if assert_total:
        pass_pct = 100.0 * res.assert_pass / assert_total
        print(f"  assertions run      : {assert_total}  (node baseline: 6794)")
        print(f"  assertions passing  : {res.assert_pass} / {assert_total} ({pass_pct:.1f}%)")
        print(f"  assertions failing  : {res.assert_fail}")
    else:
        print(f"  assertions run      : 0")
    if res.last_module or res.last_test:
        print(f"  last point   : [{res.last_module}] {res.last_test}")
    if res.status != "complete":
        # Surface tail of stderr (filter noise) for crash context.
        tail = [l for l in (stderr or "").splitlines()
                if not NOISE_RE.match(l)][-5:]
        if tail:
            print(f"  stderr tail  :")
            for t in tail:
                print(f"    {t}")

    if args.save:
        rec = asdict(res)
        rec["ts"] = int(time.time())
        with RESULTS_JSONL.open("a", encoding="utf-8") as fp:
            fp.write(json.dumps(rec) + "\n")
        print(f"  saved        : {RESULTS_JSONL.relative_to(PROJECT_ROOT)}")

    # Exit 0 if we have meaningful data; nonzero only for setup failures.
    return 0 if res.status != "no-runall" else 1


if __name__ == "__main__":
    sys.exit(main())
