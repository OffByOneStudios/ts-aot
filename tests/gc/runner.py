#!/usr/bin/env python3
"""GC verification-harness runner (GC-001).

Compiles each programs/*.js with ts-aot once, then runs it under several GC
configurations and checks:

  1. PASS contract     - each config must print `PASS` (exit 0). A `FAIL: ...`
                         line, crash, or nonzero exit is a failure.
  2. Differential      - the moving-GC run (default) must produce the SAME
                         stdout as the non-moving baseline (TS_GC_NURSERY=0).
                         A mismatch is the moving-GC corruption signature.
  3. INV-1 assert      - the TS_GC_VERIFY=2 run must not abort (no stale/
                         dangling holder survives a minor GC).

Configs (env overlays):
  default   : {}                       moving nursery, normal thresholds
  nursery0  : TS_GC_NURSERY=0          non-moving baseline (differential anchor)
  verify2   : TS_GC_VERIFY=2           INV-1 abort-on-violation
  stress    : TS_GC_STRESS=1           full collect every alloc (opt-in: --stress)

Usage:
    python tests/gc/runner.py                 # default+nursery0+verify2
    python tests/gc/runner.py --stress         # also run stress config
    python tests/gc/runner.py -k queue         # filter by program name
    python tests/gc/runner.py -j 8             # parallel
    python tests/gc/runner.py -v               # show passing detail
"""

import argparse
import concurrent.futures as cf
import hashlib
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROGRAMS_DIR = SCRIPT_DIR / "programs"
PROJECT_ROOT = SCRIPT_DIR.parent.parent
COMPILER = PROJECT_ROOT / "build" / "src" / "compiler" / "Release" / "ts-aot.exe"
BUILD_DIR = SCRIPT_DIR / "build"

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
GREEN, RED, YELLOW, CYAN, RESET = "\033[32m", "\033[31m", "\033[33m", "\033[36m", "\033[0m"

# Config name -> env overlay applied on top of os.environ.
BASE_CONFIGS = {
    "default":  {},
    "nursery0": {"TS_GC_NURSERY": "0"},
    "verify2":  {"TS_GC_VERIFY": "2"},
}
STRESS_CONFIG = {"stress": {"TS_GC_STRESS": "1"}}

# Lines emitted by the compiler/runtime that are not program output.
NOISE_RE = re.compile(r"^\[(warning|TsGC|ts-aot|LINK|TIMING)\b|^\[\w+\] ")


def _hashed_exe_name(prog_path: Path) -> str:
    rel = prog_path.relative_to(PROGRAMS_DIR).as_posix()
    h = hashlib.sha1(rel.encode("utf-8")).hexdigest()[:16]
    return ("g" + h + ".exe") if sys.platform == "win32" else ("g" + h)


def _program_stdout(out: str) -> str:
    """Strip compiler/runtime noise lines, keep only program output."""
    keep = []
    for ln in out.splitlines():
        s = ln.strip()
        if not s:
            continue
        if NOISE_RE.match(s):
            continue
        keep.append(s)
    return "\n".join(keep)


@dataclass
class ConfigRun:
    config: str
    status: str            # pass / fail / crash / abort
    reason: str = ""
    stdout: str = ""


@dataclass
class ProgramResult:
    name: str
    status: str            # pass / compile_error / fail / crash / differential
    reason: str = ""
    runs: list = field(default_factory=list)
    time_ms: float = 0.0


def run_program(prog_path: Path, configs: dict, timeout: int = 30) -> ProgramResult:
    import time as _time
    name = prog_path.stem
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    exe = BUILD_DIR / _hashed_exe_name(prog_path)
    t0 = _time.perf_counter()

    # Compile once.
    try:
        cp = subprocess.run(
            [str(COMPILER), str(prog_path), "-o", str(exe)],
            capture_output=True, timeout=timeout, text=True, cwd=str(PROJECT_ROOT),
        )
    except subprocess.TimeoutExpired:
        return ProgramResult(name, "compile_error", reason="compile timeout",
                             time_ms=(_time.perf_counter() - t0) * 1000)
    if cp.returncode != 0:
        err = (cp.stderr or "").strip().splitlines()
        reason = next((ln for ln in err if "[error]" in ln or "Error:" in ln),
                      (err[-1] if err else f"exit {cp.returncode}"))
        return ProgramResult(name, "compile_error", reason=reason[:200],
                             time_ms=(_time.perf_counter() - t0) * 1000)

    runs = []
    for cfg_name, overlay in configs.items():
        env = dict(os.environ)
        env.update(overlay)
        try:
            rp = subprocess.run([str(exe)], capture_output=True, timeout=timeout,
                                text=True, cwd=str(PROJECT_ROOT), env=env)
        except subprocess.TimeoutExpired:
            runs.append(ConfigRun(cfg_name, "crash", reason="execution timeout"))
            continue

        out = rp.stdout or ""
        err = rp.stderr or ""
        prog_out = _program_stdout(out)
        combined = out + "\n" + err

        # Runtime INV-1 abort (TS_GC_VERIFY>=2) shows up as an abort/nonzero exit
        # with the INV-1 banner on stderr.
        if "INV-1 FAILED" in err:
            runs.append(ConfigRun(cfg_name, "abort",
                                  reason="INV-1: stale/dangling holder after minor GC",
                                  stdout=prog_out))
            continue
        # Look for verdict line.
        verdict = None
        for ln in prog_out.splitlines():
            if ln == "PASS" or ln.startswith("FAIL:"):
                verdict = ln
        if rp.returncode != 0 and "FAIL:" not in combined and verdict != "PASS":
            runs.append(ConfigRun(cfg_name, "crash",
                                  reason=f"exit {rp.returncode}: {err.strip()[:160] or prog_out[:160]}",
                                  stdout=prog_out))
            continue
        if verdict == "PASS":
            runs.append(ConfigRun(cfg_name, "pass", stdout=prog_out))
        elif verdict and verdict.startswith("FAIL:"):
            runs.append(ConfigRun(cfg_name, "fail", reason=verdict[5:].strip()[:200],
                                  stdout=prog_out))
        else:
            runs.append(ConfigRun(cfg_name, "fail", reason=f"no PASS/FAIL; out={prog_out[:80]!r}",
                                  stdout=prog_out))

    elapsed = (_time.perf_counter() - t0) * 1000

    # Aggregate verdict.
    by = {r.config: r for r in runs}
    # Any non-pass config?
    for r in runs:
        if r.status != "pass":
            return ProgramResult(name, r.status if r.status in ("abort", "crash") else "fail",
                                 reason=f"[{r.config}] {r.reason}", runs=runs, time_ms=elapsed)
    # Differential: default vs nursery0 stdout must match.
    if "default" in by and "nursery0" in by:
        if by["default"].stdout != by["nursery0"].stdout:
            return ProgramResult(name, "differential",
                                 reason="moving-GC output != NURSERY=0 baseline",
                                 runs=runs, time_ms=elapsed)
    return ProgramResult(name, "pass", runs=runs, time_ms=elapsed)


def main() -> int:
    ap = argparse.ArgumentParser(description="GC verification-harness runner (GC-001)")
    ap.add_argument("-v", "--verbose", action="store_true", help="Show passing programs too")
    ap.add_argument("-k", "--filter", help="Substring match on program filename")
    ap.add_argument("-j", "--jobs", type=int, default=4, help="Parallel jobs (default 4)")
    ap.add_argument("-t", "--timeout", type=int, default=30, help="Per-run timeout (s)")
    ap.add_argument("--stress", action="store_true", help="Also run the TS_GC_STRESS=1 config")
    ap.add_argument("--include-broken", action="store_true",
                    help="Also run programs/expected_broken/* — programs distilled from "
                         "known unfixed GC bugs (currently the moving-GC corruption). These "
                         "WILL fail differentially under default nursery; they're the testbed "
                         "for an in-progress GC fix, not a regression signal. Move them back "
                         "into programs/ once the fix lands.")
    args = ap.parse_args()

    if not COMPILER.exists():
        print(f"{RED}Error:{RESET} ts-aot not built at {COMPILER}", file=sys.stderr)
        return 1
    if not PROGRAMS_DIR.exists():
        print(f"{RED}Error:{RESET} no programs directory at {PROGRAMS_DIR}", file=sys.stderr)
        return 1

    configs = dict(BASE_CONFIGS)
    if args.stress:
        configs.update(STRESS_CONFIG)

    progs = sorted(PROGRAMS_DIR.glob("*.js"))
    if args.include_broken:
        broken_dir = PROGRAMS_DIR / "expected_broken"
        if broken_dir.exists():
            progs.extend(sorted(broken_dir.glob("*.js")))
    if args.filter:
        progs = [p for p in progs if args.filter in p.name]
    if not progs:
        print("No programs to run.")
        return 0

    print(f"Running {len(progs)} GC program(s) x {len(configs)} config(s) "
          f"[{', '.join(configs)}] with j={args.jobs}...")

    results = []
    if args.jobs > 1:
        with cf.ProcessPoolExecutor(max_workers=args.jobs) as ex:
            futs = {ex.submit(run_program, p, configs, args.timeout): p for p in progs}
            for fut in cf.as_completed(futs):
                results.append(fut.result())
    else:
        for p in progs:
            results.append(run_program(p, configs, args.timeout))

    results.sort(key=lambda r: r.name)

    counts = {}
    failures = []
    for r in results:
        counts[r.status] = counts.get(r.status, 0) + 1
        sym = {"pass": GREEN + "." + RESET,
               "fail": RED + "F" + RESET,
               "compile_error": RED + "C" + RESET,
               "crash": RED + "X" + RESET,
               "differential": RED + "D" + RESET}.get(r.status, "?")
        if args.verbose or r.status != "pass":
            line = f"  {sym} {r.name:<40} {r.time_ms:>7.0f}ms"
            if r.reason:
                line += f"  {CYAN}{r.reason}{RESET}"
            print(line)
        if r.status != "pass":
            failures.append(r)

    print()
    print("=" * 60)
    npass = counts.get("pass", 0)
    print(f"  Passed:        {GREEN}{npass:>4}{RESET}")
    print(f"  Failed:        {RED}{counts.get('fail', 0):>4}{RESET}")
    print(f"  Differential:  {RED}{counts.get('differential', 0):>4}{RESET}  (moving-GC corruption)")
    print(f"  Crash/abort:   {RED}{counts.get('crash', 0) + counts.get('abort', 0):>4}{RESET}")
    print(f"  Compile error: {RED}{counts.get('compile_error', 0):>4}{RESET}")
    print(f"  Total:         {len(results):>4}")
    print("=" * 60)

    if failures:
        print(f"\n{RED}GC failures:{RESET}")
        for f in failures:
            print(f"  - {f.name} [{f.status}]: {f.reason}")
        return 1
    print(f"{GREEN}All GC programs pass in all configs (differential clean).{RESET}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
