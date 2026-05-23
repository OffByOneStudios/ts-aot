#!/usr/bin/env python3
"""Lodash test runner.

Compiles each *.ts in tests/lodash/ (except files starting with _) and runs
it. The test prints `OK: <category>` on success and `FAIL: <category>` with
detail lines on failure.

A runtime panic (compiler-emitted "Runtime Panic: ...") aborts the test's
process — that's recorded as PANIC for the category. Other tests in other
categories still run because each is a separate subprocess.
"""

import argparse
import re
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path


def find_compiler() -> Path:
    """Locate the built ts-aot compiler binary."""
    root = Path(__file__).resolve().parent.parent.parent
    candidates = [
        root / "build" / "src" / "compiler" / "Release" / "ts-aot.exe",
        root / "build" / "src" / "compiler" / "Release" / "ts-aot",
    ]
    for c in candidates:
        if c.exists():
            return c
    print("Could not find ts-aot compiler. Build with: cmake --build build --config Release", file=sys.stderr)
    sys.exit(2)


def run_one(compiler: Path, src: Path, verbose: bool = False) -> dict:
    """Compile + run one test file, return result dict."""
    tmp_dir = Path(tempfile.gettempdir()) / f"lodash_test_{uuid.uuid4().hex[:12]}"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    exe = tmp_dir / (src.stem + ".exe")

    # Compile
    cp = subprocess.run(
        [str(compiler), str(src), "-o", str(exe)],
        capture_output=True, text=True, timeout=120,
    )
    if cp.returncode != 0 or not exe.exists():
        return {
            "name": src.stem,
            "status": "COMPILE_ERROR",
            "passed": 0, "failed": 0,
            "detail": cp.stderr[-500:] if cp.stderr else "(no stderr)",
        }

    # Run
    try:
        rp = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return {"name": src.stem, "status": "TIMEOUT", "passed": 0, "failed": 0, "detail": ""}

    out = rp.stdout
    err = rp.stderr

    # Look for "OK: <name>" / "FAIL: <name> (N passed, M failed)"
    m_ok = re.search(r"^OK:\s*(\S+)\s*(?:\((\d+)\s+passed\))?", out, re.MULTILINE)
    m_fail = re.search(r"^FAIL:\s*(\S+)\s*\((\d+)\s+passed,\s*(\d+)\s+failed\)", out, re.MULTILINE)

    if "Runtime Panic" in out or "Runtime Panic" in err:
        passed = int(m_fail.group(2)) if m_fail else 0
        return {"name": src.stem, "status": "PANIC", "passed": passed, "failed": 0,
                "detail": (out + "\n" + err)[-800:]}

    if m_fail:
        return {"name": src.stem, "status": "FAIL", "passed": int(m_fail.group(2)),
                "failed": int(m_fail.group(3)), "detail": out}

    if m_ok:
        passed = int(m_ok.group(2)) if m_ok.group(2) else 0
        return {"name": src.stem, "status": "OK", "passed": passed, "failed": 0, "detail": out}

    return {"name": src.stem, "status": "UNKNOWN", "passed": 0, "failed": 0,
            "detail": (out + "\n--- stderr ---\n" + err)[-800:]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pattern", default="*.ts", help="Glob for test files in tests/lodash/")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    compiler = find_compiler()
    test_dir = Path(__file__).resolve().parent

    files = sorted(p for p in test_dir.glob(args.pattern)
                   if p.is_file() and not p.name.startswith("_"))
    if not files:
        print(f"No tests matched {args.pattern}", file=sys.stderr)
        sys.exit(1)

    results = []
    total_passed = 0
    total_failed = 0
    total_panic = 0
    for src in files:
        r = run_one(compiler, src, args.verbose)
        results.append(r)
        status = r["status"]
        if status == "OK":
            print(f"  OK  {r['name']:<14} {r['passed']} passed")
            total_passed += r["passed"]
        elif status == "FAIL":
            print(f"  FAIL {r['name']:<14} {r['passed']} passed, {r['failed']} failed")
            if args.verbose:
                print(r["detail"])
            total_passed += r["passed"]
            total_failed += r["failed"]
        elif status == "PANIC":
            print(f"  PANIC {r['name']:<14} (crashed; {r['passed']} tests ran before crash)")
            total_panic += 1
            total_passed += r["passed"]
            if args.verbose:
                print(r["detail"])
        else:
            print(f"  {status:<5} {r['name']:<14}")
            print(r["detail"])
            total_panic += 1

    print()
    print(f"Total: {total_passed} passed, {total_failed} failed, {total_panic} panic/error across {len(files)} files")
    sys.exit(0 if (total_failed == 0 and total_panic == 0) else 1)


if __name__ == "__main__":
    main()
