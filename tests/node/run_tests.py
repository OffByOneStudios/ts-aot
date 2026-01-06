#!/usr/bin/env python3
"""Node.js API Test Runner - Compiles and runs Node.js API tests."""

import argparse
import os
import subprocess
import sys
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional


@dataclass
class TestResult:
    """Result of running a test."""
    name: str
    passed: bool
    skipped: bool = False
    blocked: bool = False
    exit_code: int = 0
    output: str = ""
    error: str = ""
    compile_error: bool = False


class Colors:
    """ANSI color codes for terminal output."""
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    CYAN = '\033[96m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

    @staticmethod
    def enabled():
        """Check if colors should be enabled."""
        return sys.stdout.isatty() and os.name != 'nt' or os.environ.get('TERM')


def color_text(text: str, color: str) -> str:
    """Apply color to text if colors are enabled."""
    if Colors.enabled():
        return f"{color}{text}{Colors.RESET}"
    return text


class NodeTestRunner:
    """Main test runner for Node.js API tests."""

    def __init__(self, test_pattern: str = "**/*.ts", verbose: bool = False):
        self.script_dir = Path(__file__).parent
        self.root_dir = self.script_dir.parent.parent
        self.compiler_path = self.root_dir / 'build' / 'src' / 'compiler' / 'Release' / 'ts-aot.exe'
        self.test_pattern = test_pattern
        self.verbose = verbose

        self.total_tests = 0
        self.passed_tests = 0
        self.failed_tests = 0
        self.skipped_tests = 0
        self.blocked_tests = 0
        self.compile_errors = 0

        self.results: List[TestResult] = []

    def check_compiler_exists(self) -> bool:
        """Verify that the compiler exists."""
        if not self.compiler_path.exists():
            print(color_text(f"ERROR: Compiler not found at {self.compiler_path}", Colors.RED))
            print("Please build the compiler first.")
            return False
        return True

    def find_test_files(self) -> List[Path]:
        """Find all test TypeScript files."""
        test_files = []

        # Find all .ts files in subdirectories, excluding test_template.ts
        for category_dir in self.script_dir.iterdir():
            if category_dir.is_dir() and not category_dir.name.startswith('.'):
                for test_file in category_dir.glob("*.ts"):
                    if test_file.name != 'test_template.ts':
                        test_files.append(test_file)

        return sorted(test_files)

    def compile_test(self, test_file: Path) -> tuple[bool, str]:
        """Compile a test file to an executable."""
        exe_path = test_file.with_suffix('.exe')

        try:
            result = subprocess.run(
                [str(self.compiler_path), str(test_file), '-o', str(exe_path)],
                capture_output=True,
                text=True,
                timeout=30
            )

            if result.returncode != 0:
                return False, result.stderr

            return True, ""
        except subprocess.TimeoutExpired:
            return False, "Compilation timeout"
        except Exception as e:
            return False, str(e)

    def run_test(self, exe_path: Path) -> tuple[int, str, str]:
        """Run a test executable."""
        try:
            result = subprocess.run(
                [str(exe_path)],
                capture_output=True,
                text=True,
                timeout=10
            )

            return result.returncode, result.stdout, result.stderr
        except subprocess.TimeoutExpired:
            return -1, "", "Test execution timeout"
        except Exception as e:
            return -1, "", str(e)

    def check_test_status(self, output: str) -> tuple[bool, bool]:
        """Check if test is skipped or blocked based on output."""
        blocked_keywords = [
            "blocked pending async",
            "blocked pending event loop",
            "blocked by async bug",
            "blocked by event loop",
            "blocked by array access bug",
            "blocked by process module bug"
        ]

        output_lower = output.lower()

        for keyword in blocked_keywords:
            if keyword in output_lower:
                return False, True  # Not skipped, but blocked

        return False, False

    def run_single_test(self, test_file: Path) -> TestResult:
        """Compile and run a single test."""
        test_name = f"{test_file.parent.name}/{test_file.name}"
        exe_path = test_file.with_suffix('.exe')

        if self.verbose:
            print(f"  Compiling {test_name}...")

        # Compile
        compile_success, compile_error = self.compile_test(test_file)
        if not compile_success:
            return TestResult(
                name=test_name,
                passed=False,
                compile_error=True,
                error=compile_error
            )

        if self.verbose:
            print(f"  Running {test_name}...")

        # Run
        exit_code, output, error = self.run_test(exe_path)

        # Check status
        skipped, blocked = self.check_test_status(output)

        # Clean up executable
        if exe_path.exists():
            try:
                exe_path.unlink()
            except:
                pass

        # Determine pass/fail
        passed = exit_code == 0 and not blocked

        return TestResult(
            name=test_name,
            passed=passed,
            skipped=skipped,
            blocked=blocked,
            exit_code=exit_code,
            output=output,
            error=error
        )

    def print_test_result(self, result: TestResult):
        """Print a single test result."""
        status_width = 10

        if result.compile_error:
            status = color_text("COMPILE ERR".ljust(status_width), Colors.RED)
        elif result.blocked:
            status = color_text("BLOCKED".ljust(status_width), Colors.YELLOW)
        elif result.skipped:
            status = color_text("SKIPPED".ljust(status_width), Colors.CYAN)
        elif result.passed:
            status = color_text("PASS".ljust(status_width), Colors.GREEN)
        else:
            status = color_text("FAIL".ljust(status_width), Colors.RED)

        print(f"  {status} {result.name}")

        if self.verbose or (not result.passed and not result.blocked and not result.skipped):
            if result.compile_error:
                print(f"    Compile error: {result.error}")
            elif result.error:
                print(f"    Error: {result.error}")
            if result.output and not result.passed:
                # Show last few lines of output
                lines = result.output.strip().split('\n')
                for line in lines[-5:]:
                    print(f"    {line}")

    def run_all_tests(self):
        """Run all tests and report results."""
        if not self.check_compiler_exists():
            return 1

        print(color_text("\n=== Node.js API Test Suite ===\n", Colors.BOLD))

        test_files = self.find_test_files()

        if not test_files:
            print(color_text("No test files found!", Colors.RED))
            return 1

        print(f"Found {len(test_files)} test file(s)\n")

        for test_file in test_files:
            result = self.run_single_test(test_file)
            self.results.append(result)
            self.print_test_result(result)

            self.total_tests += 1
            if result.compile_error:
                self.compile_errors += 1
                self.failed_tests += 1
            elif result.blocked:
                self.blocked_tests += 1
            elif result.skipped:
                self.skipped_tests += 1
            elif result.passed:
                self.passed_tests += 1
            else:
                self.failed_tests += 1

        self.print_summary()

        return 0 if self.failed_tests == 0 and self.compile_errors == 0 else 1

    def print_summary(self):
        """Print test run summary."""
        print(color_text("\n=== Test Summary ===\n", Colors.BOLD))

        print(f"Total tests:       {self.total_tests}")
        print(color_text(f"Passed:            {self.passed_tests}", Colors.GREEN))
        print(color_text(f"Failed:            {self.failed_tests}", Colors.RED))
        print(color_text(f"Blocked:           {self.blocked_tests}", Colors.YELLOW))
        print(color_text(f"Skipped:           {self.skipped_tests}", Colors.CYAN))
        print(color_text(f"Compile errors:    {self.compile_errors}", Colors.RED))

        if self.passed_tests > 0:
            pass_rate = (self.passed_tests / (self.total_tests - self.blocked_tests - self.skipped_tests)) * 100 if (self.total_tests - self.blocked_tests - self.skipped_tests) > 0 else 0
            print(f"\nPass rate (excluding blocked/skipped): {pass_rate:.1f}%")


def main():
    parser = argparse.ArgumentParser(description='Run Node.js API tests')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    parser.add_argument('-p', '--pattern', default='**/*.ts', help='Test file pattern')

    args = parser.parse_args()

    runner = NodeTestRunner(test_pattern=args.pattern, verbose=args.verbose)
    return runner.run_all_tests()


if __name__ == '__main__':
    sys.exit(main())
