#!/usr/bin/env python3
"""
Integration Test Runner for ts-aot

Compiles and runs multi-file TypeScript integration tests that exercise
real-world patterns not covered by unit tests.

Usage:
    python run_integration_tests.py [--category CATEGORY] [--test TEST]

Categories:
    cross_module  - Cross-module object passing tests
    gc_stress     - GC stress tests (high allocation)
    patterns      - Real-world code patterns
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path
from typing import List, Tuple, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ts_test_platform import get_compiler_path, get_exe_suffix

# Configuration
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
COMPILER = get_compiler_path(PROJECT_ROOT)

# Test categories and their entry points
TESTS = {
    "cross_module": [
        ("simple_object", "simple_object.ts"),
        ("nested_object", "nested_object.ts"),
        ("array_of_objects", "array_of_objects.ts"),
        ("callback_with_object", "callback_with_object.ts"),
        ("re_export", "re_export.ts"),
    ],
    "gc_stress": [
        ("string_allocation", "string_allocation.ts"),
        ("object_allocation", "object_allocation.ts"),
        ("nested_allocation", "nested_allocation.ts"),
        ("loop_allocation", "loop_allocation.ts"),
        ("json_roundtrip", "json_roundtrip.ts"),
    ],
    "patterns": [
        ("class_hierarchy", "class_hierarchy.ts"),
        ("event_emitter", "event_emitter.ts"),
        ("array_methods", "array_methods.ts"),
        ("map_set_operations", "map_set_operations.ts"),
    ],
}


def compile_test(category: str, entry_file: str) -> Tuple[bool, str, Optional[Path]]:
    """Compile a test and return (success, message, exe_path)"""
    test_dir = SCRIPT_DIR / category
    entry_path = test_dir / entry_file

    if not entry_path.exists():
        return False, f"Entry file not found: {entry_path}", None

    # Output executable in same directory
    exe_name = entry_file.replace(".ts", get_exe_suffix())
    exe_path = test_dir / exe_name

    cmd = [str(COMPILER), str(entry_path), "-o", str(exe_path)]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60
        )

        if result.returncode != 0:
            return False, f"Compilation failed:\n{result.stderr}", None

        return True, "Compiled successfully", exe_path

    except subprocess.TimeoutExpired:
        return False, "Compilation timed out", None
    except Exception as e:
        return False, f"Compilation error: {e}", None


def run_test(exe_path: Path) -> Tuple[bool, str, int]:
    """Run a compiled test and return (success, output, exit_code)"""
    try:
        result = subprocess.run(
            [str(exe_path)],
            capture_output=True,
            text=True,
            timeout=120
        )

        success = result.returncode == 0 and "PASS" in result.stdout
        output = result.stdout
        if result.stderr:
            output += "\n--- STDERR ---\n" + result.stderr

        return success, output, result.returncode

    except subprocess.TimeoutExpired:
        return False, "Test timed out", -1
    except Exception as e:
        return False, f"Execution error: {e}", -1


def run_category(category: str, specific_test: Optional[str] = None) -> Tuple[int, int]:
    """Run all tests in a category. Returns (passed, failed)"""
    tests = TESTS.get(category, [])
    if not tests:
        print(f"Unknown category: {category}")
        return 0, 0

    passed = 0
    failed = 0

    print(f"\n{'='*60}")
    print(f"Category: {category}")
    print(f"{'='*60}")

    for test_name, entry_file in tests:
        if specific_test and test_name != specific_test:
            continue

        print(f"\n--- {test_name} ---")

        # Compile
        compile_ok, compile_msg, exe_path = compile_test(category, entry_file)
        if not compile_ok:
            print(f"COMPILE FAIL: {compile_msg}")
            failed += 1
            continue

        # Run
        run_ok, output, exit_code = run_test(exe_path)

        if run_ok:
            print(f"PASS (exit code: {exit_code})")
            passed += 1
        else:
            print(f"FAIL (exit code: {exit_code})")
            print("Output:")
            for line in output.split('\n')[:20]:  # First 20 lines
                print(f"  {line}")
            if len(output.split('\n')) > 20:
                print(f"  ... ({len(output.split(chr(10))) - 20} more lines)")
            failed += 1

        # Cleanup exe
        if exe_path and exe_path.exists():
            try:
                exe_path.unlink()
            except:
                pass

    return passed, failed


def main():
    parser = argparse.ArgumentParser(description="Run ts-aot integration tests")
    parser.add_argument("--category", "-c", help="Run specific category")
    parser.add_argument("--test", "-t", help="Run specific test within category")
    parser.add_argument("--list", "-l", action="store_true", help="List all tests")
    args = parser.parse_args()

    # Check compiler exists
    if not COMPILER.exists():
        print(f"ERROR: Compiler not found at {COMPILER}")
        print("Build the compiler first: cmake --build build --config Release")
        return 1

    # List tests
    if args.list:
        for cat, tests in TESTS.items():
            print(f"\n{cat}:")
            for name, entry in tests:
                print(f"  {name} ({entry})")
        return 0

    # Run tests
    total_passed = 0
    total_failed = 0

    categories = [args.category] if args.category else TESTS.keys()

    for category in categories:
        passed, failed = run_category(category, args.test)
        total_passed += passed
        total_failed += failed

    # Summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    print(f"Passed: {total_passed}")
    print(f"Failed: {total_failed}")
    print(f"Total:  {total_passed + total_failed}")

    if total_failed > 0:
        print("\nSome tests FAILED")
        return 1
    else:
        print("\nAll tests PASSED")
        return 0


if __name__ == "__main__":
    sys.exit(main())
