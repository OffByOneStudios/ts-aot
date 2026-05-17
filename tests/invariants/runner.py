#!/usr/bin/env python3
"""Spec-invariant probe runner.

Compiles each probes/*.js with ts-aot and runs it. A probe must print
exactly `PASS` on success or `FAIL: <reason>` on failure. Anything else
(empty output, crash, compile error) is recorded as a failure mode.

Usage:
    python tests/invariants/runner.py            # run all, summary
    python tests/invariants/runner.py -v         # show pass lines too
    python tests/invariants/runner.py -k delete  # only probes matching name
    python tests/invariants/runner.py -j 8       # parallel
"""

import argparse
import concurrent.futures as cf
import hashlib
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROBES_DIR = SCRIPT_DIR / "probes"
PROJECT_ROOT = SCRIPT_DIR.parent.parent
COMPILER = PROJECT_ROOT / "build" / "src" / "compiler" / "Release" / "ts-aot.exe"
BUILD_DIR = SCRIPT_DIR / "build"


def _hashed_exe_name(probe_path: Path) -> str:
    """Windows UAC installer-detection heuristic flags filenames containing
    "delete", "install", "update", "setup", "patch" etc. as installers and
    demands elevation (WinError 740). Hash the probe name to bypass it —
    same fix as tests/test262/run_test262.py."""
    rel = probe_path.relative_to(PROBES_DIR).as_posix()
    h = hashlib.sha1(rel.encode("utf-8")).hexdigest()[:16]
    return ("p" + h + ".exe") if sys.platform == "win32" else ("p" + h)

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
GREEN, RED, YELLOW, CYAN, RESET = "\033[32m", "\033[31m", "\033[33m", "\033[36m", "\033[0m"


@dataclass
class ProbeResult:
    name: str
    status: str  # pass / fail / compile_error / crash / no_output
    reason: str = ""
    time_ms: float = 0.0


def run_probe(probe_path: Path, timeout: int = 10) -> ProbeResult:
    """Compile + run one probe. Returns ProbeResult."""
    import time as _time
    name = probe_path.stem
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    exe = BUILD_DIR / _hashed_exe_name(probe_path)

    t0 = _time.perf_counter()

    # Compile
    try:
        cp = subprocess.run(
            [str(COMPILER), str(probe_path), "-o", str(exe)],
            capture_output=True, timeout=timeout, text=True,
            cwd=str(PROJECT_ROOT),
        )
    except subprocess.TimeoutExpired:
        return ProbeResult(name, "compile_error", reason="compile timeout",
                           time_ms=(_time.perf_counter() - t0) * 1000)
    if cp.returncode != 0:
        # Pluck the first error line from stderr
        err = (cp.stderr or "").strip().splitlines()
        reason = next((ln for ln in err if "[error]" in ln or "Error:" in ln),
                      (err[-1] if err else f"exit {cp.returncode}"))
        return ProbeResult(name, "compile_error", reason=reason[:200],
                           time_ms=(_time.perf_counter() - t0) * 1000)

    # Execute
    try:
        rp = subprocess.run(
            [str(exe)], capture_output=True, timeout=timeout, text=True,
            cwd=str(PROJECT_ROOT),
        )
    except subprocess.TimeoutExpired:
        return ProbeResult(name, "crash", reason="execution timeout",
                           time_ms=(_time.perf_counter() - t0) * 1000)

    out = (rp.stdout or "").strip()
    err = (rp.stderr or "").strip()
    elapsed = (_time.perf_counter() - t0) * 1000

    # Look for PASS / FAIL: tokens. Stderr may carry a FATAL from the runtime
    # — surface that as a crash.
    combined = out + "\n" + err
    if "FATAL:" in err and "Uncaught" in err:
        # Pull the message line if present
        m = re.search(r"\.message\s*=\s*(.+)", err)
        msg = m.group(1).strip() if m else "uncaught exception"
        return ProbeResult(name, "crash", reason=f"uncaught: {msg[:160]}",
                           time_ms=elapsed)
    if rp.returncode != 0 and "FAIL:" not in combined:
        return ProbeResult(name, "crash",
                           reason=f"exit {rp.returncode}: {err[:160] or out[:160]}",
                           time_ms=elapsed)

    # Find last PASS or FAIL line — multiple lines in a probe are allowed but
    # only the final verdict matters
    last_line = None
    for line in out.splitlines():
        line = line.strip()
        if line == "PASS" or line.startswith("FAIL:"):
            last_line = line
    if last_line == "PASS":
        return ProbeResult(name, "pass", time_ms=elapsed)
    if last_line and last_line.startswith("FAIL:"):
        return ProbeResult(name, "fail", reason=last_line[5:].strip()[:200],
                           time_ms=elapsed)
    return ProbeResult(name, "no_output",
                       reason=f"no PASS/FAIL line; stdout={out[:100]!r}",
                       time_ms=elapsed)


def main() -> int:
    parser = argparse.ArgumentParser(description="Spec-invariant probe runner")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show passing probes too")
    parser.add_argument("-k", "--filter",
                        help="Substring match on probe filename")
    parser.add_argument("-j", "--jobs", type=int, default=4,
                        help="Parallel jobs (default: 4)")
    parser.add_argument("-t", "--timeout", type=int, default=10,
                        help="Per-probe timeout (seconds)")
    args = parser.parse_args()

    if not COMPILER.exists():
        print(f"{RED}Error:{RESET} ts-aot not built at {COMPILER}", file=sys.stderr)
        print("Build with: cmake --build build --config Release --target ts-aot",
              file=sys.stderr)
        return 1
    if not PROBES_DIR.exists():
        print(f"{RED}Error:{RESET} no probes directory at {PROBES_DIR}", file=sys.stderr)
        return 1

    probes = sorted(PROBES_DIR.glob("*.js"))
    if args.filter:
        probes = [p for p in probes if args.filter in p.name]
    if not probes:
        print("No probes to run.")
        return 0

    print(f"Running {len(probes)} probe(s) with j={args.jobs}...")

    results = []
    if args.jobs > 1:
        with cf.ProcessPoolExecutor(max_workers=args.jobs) as ex:
            futs = {ex.submit(run_probe, p, args.timeout): p for p in probes}
            for fut in cf.as_completed(futs):
                results.append(fut.result())
    else:
        for p in probes:
            results.append(run_probe(p, args.timeout))

    results.sort(key=lambda r: r.name)

    counts = {"pass": 0, "fail": 0, "compile_error": 0, "crash": 0, "no_output": 0}
    failures = []
    for r in results:
        counts[r.status] = counts.get(r.status, 0) + 1
        sym = {"pass": GREEN + "." + RESET,
               "fail": RED + "F" + RESET,
               "compile_error": RED + "C" + RESET,
               "crash": RED + "X" + RESET,
               "no_output": YELLOW + "?" + RESET}[r.status]
        if args.verbose or r.status != "pass":
            line = f"  {sym} {r.name:<50} {r.time_ms:>6.0f}ms"
            if r.reason:
                line += f"  {CYAN}{r.reason}{RESET}"
            print(line)
        if r.status != "pass":
            failures.append(r)

    print()
    print("=" * 60)
    print(f"  Passed:        {GREEN}{counts['pass']:>4}{RESET}")
    print(f"  Failed:        {RED}{counts['fail']:>4}{RESET}")
    print(f"  Compile error: {RED}{counts['compile_error']:>4}{RESET}")
    print(f"  Crash:         {RED}{counts['crash']:>4}{RESET}")
    print(f"  No output:     {YELLOW}{counts['no_output']:>4}{RESET}")
    print(f"  Total:         {len(results):>4}")
    print("=" * 60)

    if failures:
        print(f"\n{RED}Failures (probable spec bugs):{RESET}")
        for f in failures:
            print(f"  - {f.name} [{f.status}]: {f.reason}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
