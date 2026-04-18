#!/usr/bin/env python3
"""Run a test262 test against Node.js (V8) and ts-aot side-by-side.

Compares stdout/exit from V8 (oracle) vs ts-aot, so you can see what the
spec-correct behavior should be without parsing the test assertion format
manually.

Usage:
    python tests/test262/oracle.py <test-path>
      where test-path is relative to tests/test262/test262/test/

    Examples:
      python tests/test262/oracle.py built-ins/Array/prototype/every/15.4.4.16-1-11.js
      python tests/test262/oracle.py annexB/built-ins/Date/prototype/getYear/nan.js

Options:
    --no-harness     Skip test262 harness stubs (just run the raw file)
    --only v8|tsaot  Run only one side (default: both)
    --show-source    Print relevant bits of the test source
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
TEST262_DIR = SCRIPT_DIR / "test262"
HARNESS_DIR = TEST262_DIR / "harness"
TEST_DIR = TEST262_DIR / "test"

# Pull compiler path
sys.path.insert(0, str(SCRIPT_DIR.parent))
from ts_test_platform import get_compiler_path, get_exe_suffix  # type: ignore


def parse_frontmatter_includes(src: str):
    """Extract `includes: [...]` from /*--- ... ---*/ YAML frontmatter."""
    import re
    m = re.search(r"/\*---(.+?)---\*/", src, re.DOTALL)
    if not m:
        return []
    fm = m.group(1)
    inc = re.search(r"^\s*includes:\s*\[([^\]]*)\]", fm, re.MULTILINE)
    if not inc:
        return []
    return [s.strip() for s in inc.group(1).split(",") if s.strip()]


def build_full_source(test_path: Path, skip_harness: bool) -> str:
    """Concatenate harness + test source the way the runner does."""
    parts = []
    test_src = test_path.read_text(encoding="utf-8", errors="replace")

    if not skip_harness:
        # Base harness files that every test262 test gets
        for f in ("sta.js", "assert.js"):
            p = HARNESS_DIR / f
            if p.exists():
                parts.append(p.read_text(encoding="utf-8"))
        # Test-specific includes
        for inc in parse_frontmatter_includes(test_src):
            p = HARNESS_DIR / inc
            if p.exists():
                parts.append(p.read_text(encoding="utf-8"))

    parts.append(test_src)
    return "\n\n".join(parts)


def run_node(src: str, timeout: int = 10):
    """Run via node and capture output."""
    try:
        # Pipe source via stdin; node reads from /dev/stdin when no file
        r = subprocess.run(
            ["node", "--input-type=commonjs", "-e", src],
            capture_output=True, text=True, timeout=timeout,
        )
        return r.returncode, r.stdout, r.stderr
    except FileNotFoundError:
        return -1, "", "node not installed"
    except subprocess.TimeoutExpired:
        return -1, "", f"node TIMEOUT after {timeout}s"


def run_tsaot(src: str, tmpdir: Path, timeout: int = 10):
    """Compile with ts-aot, run the exe, capture output."""
    compiler = get_compiler_path()
    suffix = get_exe_suffix()
    js_file = tmpdir / "oracle_probe.js"
    exe_file = tmpdir / f"oracle_probe{suffix}"
    js_file.write_text(src, encoding="utf-8")

    try:
        comp = subprocess.run(
            [compiler, str(js_file), "-o", str(exe_file)],
            capture_output=True, text=True, timeout=60,
        )
    except subprocess.TimeoutExpired:
        return -1, "", "compile TIMEOUT"
    if comp.returncode != 0:
        return comp.returncode, comp.stdout, f"COMPILE FAILED:\n{comp.stderr}"

    try:
        run = subprocess.run(
            [str(exe_file)],
            capture_output=True, text=True, timeout=timeout,
        )
        return run.returncode, run.stdout, run.stderr
    except subprocess.TimeoutExpired:
        return -1, "", f"ts-aot run TIMEOUT after {timeout}s"


def trim(s: str, n: int = 800) -> str:
    if len(s) <= n:
        return s
    return s[:n] + f"\n... [{len(s)-n} bytes truncated]"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("test_path",
                    help="Path to .js test file, relative to "
                         "tests/test262/test262/test/")
    ap.add_argument("--no-harness", action="store_true",
                    help="Skip test262 harness stubs")
    ap.add_argument("--only", choices=["v8", "tsaot"], default=None,
                    help="Run only one engine (default: both)")
    ap.add_argument("--show-source", action="store_true",
                    help="Print the concatenated source")
    ap.add_argument("--timeout", type=int, default=10,
                    help="Per-engine run timeout in seconds")
    args = ap.parse_args()

    rel = args.test_path.replace("\\", "/")
    # Allow either "built-ins/..." or "test/built-ins/..." or full path
    candidates = [
        TEST_DIR / rel,
        TEST262_DIR / rel,
        Path(rel),
    ]
    test_path = next((p for p in candidates if p.exists()), None)
    if not test_path:
        print(f"Error: couldn't find test at any of: {candidates}")
        sys.exit(2)

    src = build_full_source(test_path, args.no_harness)
    if args.show_source:
        print("=" * 78)
        print("SOURCE")
        print("=" * 78)
        print(src)
        print()

    print("=" * 78)
    print(f"Test: {test_path}")
    print("=" * 78)

    import tempfile
    with tempfile.TemporaryDirectory() as td:
        tmpdir = Path(td)

        if args.only != "tsaot":
            print("\n--- V8 (node) ---")
            rc, out, err = run_node(src, args.timeout)
            print(f"exit={rc}")
            if out:
                print(f"stdout:\n{trim(out)}")
            if err:
                print(f"stderr:\n{trim(err)}")

        if args.only != "v8":
            print("\n--- ts-aot ---")
            rc, out, err = run_tsaot(src, tmpdir, args.timeout)
            print(f"exit={rc}")
            if out:
                print(f"stdout:\n{trim(out)}")
            if err:
                print(f"stderr:\n{trim(err)}")


if __name__ == "__main__":
    main()
