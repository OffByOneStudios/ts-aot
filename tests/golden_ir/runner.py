#!/usr/bin/env python3
"""Golden IR Test Runner - Validates LLVM IR output against CHECK patterns."""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path
from typing import List, Dict, Optional, Tuple
from dataclasses import dataclass, field
from enum import Enum


class CheckType(Enum):
    """Type of CHECK pattern."""
    CHECK = "CHECK"
    CHECK_NOT = "NOT"
    CHECK_NEXT = "NEXT"
    CHECK_DAG = "DAG"


@dataclass
class CheckPattern:
    """A single CHECK pattern from a test file."""
    type: CheckType
    pattern: str
    line: int


@dataclass
class TestCase:
    """Represents a single test file and its expectations."""
    file_path: Path
    file_name: str
    run_command: Optional[str] = None
    check_patterns: List[CheckPattern] = field(default_factory=list)
    expected_output: List[str] = field(default_factory=list)
    expected_exit_code: int = 0
    expected_failure: bool = False
    expected_failure_reason: str = ""


@dataclass
class TestResult:
    """Result of running a test."""
    success: bool
    stage: str = ""
    error: Optional[str] = None
    failures: List[str] = field(default_factory=list)
    ir_output: str = ""
    runtime_output: str = ""
    exit_code: int = 0


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


class GoldenIRRunner:
    """Main test runner class."""

    def __init__(self, test_path: str, show_details: bool = False):
        self.script_dir = Path(__file__).parent
        self.root_dir = self.script_dir.parent.parent
        self.compiler_path = self.root_dir / 'build' / 'src' / 'compiler' / 'Release' / 'ts-aot.exe'
        self.test_path = test_path
        self.show_details = show_details

        self.total_tests = 0
        self.passed_tests = 0
        self.failed_tests = 0
        self.xfail_tests = 0  # Expected failures that failed
        self.xpass_tests = 0  # Expected failures that passed (good news!)
        self.failure_details = []

    def check_compiler_exists(self) -> bool:
        """Verify that the compiler exists."""
        if not self.compiler_path.exists():
            print(color_text(f"ERROR: Compiler not found at {self.compiler_path}", Colors.RED))
            print(color_text("Run: cmake --build build --config Release", Colors.YELLOW))
            return False
        return True

    def get_test_files(self) -> List[Path]:
        """Get list of test files to run."""
        full_path = self.script_dir / self.test_path

        if full_path.is_file():
            return [full_path]
        elif full_path.is_dir():
            # Recursively find all .ts and .js files
            test_files = []
            for ext in ['*.ts', '*.js']:
                test_files.extend(full_path.rglob(ext))
            return sorted(test_files)
        else:
            print(color_text(f"ERROR: Test path not found: {self.test_path}", Colors.RED))
            sys.exit(1)

    def parse_test_file(self, file_path: Path) -> TestCase:
        """Parse a test file to extract RUN commands, CHECK patterns, and expected output."""
        test = TestCase(file_path=file_path, file_name=file_path.name)

        with open(file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()

        for line_num, line in enumerate(lines, start=1):
            line = line.strip()

            # RUN command
            if match := re.match(r'^\s*//\s*RUN:\s*(.+)$', line):
                test.run_command = match.group(1).strip()

            # CHECK patterns
            elif match := re.match(r'^\s*//\s*CHECK(-NEXT|-NOT|-DAG)?:\s*(.+)$', line):
                check_suffix = match.group(1)
                pattern = match.group(2).strip()

                if check_suffix == '-NOT':
                    check_type = CheckType.CHECK_NOT
                elif check_suffix == '-NEXT':
                    check_type = CheckType.CHECK_NEXT
                elif check_suffix == '-DAG':
                    check_type = CheckType.CHECK_DAG
                else:
                    check_type = CheckType.CHECK

                test.check_patterns.append(CheckPattern(
                    type=check_type,
                    pattern=pattern,
                    line=line_num
                ))

            # Expected output
            elif match := re.match(r'^\s*//\s*OUTPUT:\s*(.+)$', line):
                test.expected_output.append(match.group(1).strip())

            # Expected exit code
            elif match := re.match(r'^\s*//\s*EXIT-CODE:\s*(\d+)$', line):
                test.expected_exit_code = int(match.group(1))

            # Expected failure (XFAIL)
            elif match := re.match(r'^\s*//\s*XFAIL:\s*(.+)$', line):
                test.expected_failure = True
                test.expected_failure_reason = match.group(1).strip()

        # Default RUN command if not specified
        if not test.run_command:
            test.run_command = '%ts-aot %s --dump-ir -o %t.exe && %t.exe'

        return test

    def expand_run_command(self, command: str, test_file: Path, temp_dir: Path) -> Tuple[str, Path, Path]:
        """Expand placeholders in RUN command."""
        exe_name = test_file.stem
        exe_path = temp_dir / f"{exe_name}.exe"
        ir_path = temp_dir / f"{exe_name}.ll"

        expanded = command
        expanded = expanded.replace('%ts-aot', str(self.compiler_path))
        expanded = expanded.replace('%s', str(test_file))
        expanded = expanded.replace('%t.exe', str(exe_path))
        expanded = expanded.replace('%t.ll', str(ir_path))
        expanded = expanded.replace('%t', str(temp_dir / exe_name))

        return expanded, exe_path, ir_path

    def run_test_compilation(self, test: TestCase, temp_dir: Path) -> TestResult:
        """Compile and run a test."""
        expanded_cmd, exe_path, ir_path = self.expand_run_command(
            test.run_command, test.file_path, temp_dir
        )

        # Debug: Print the expanded command
        if self.show_details:
            print(color_text(f"  Expanded command: {expanded_cmd}", Colors.YELLOW))

        # Split on && to get separate commands
        commands = [cmd.strip() for cmd in expanded_cmd.split('&&')]

        ir_output = ""
        runtime_output = ""
        exit_code = 0

        try:
            for cmd in commands:
                if '--dump-ir' in cmd:
                    # Compilation with IR dump
                    if self.show_details:
                        print(color_text(f"  Running: {cmd}", Colors.CYAN))

                    result = subprocess.run(
                        cmd,
                        shell=True,
                        capture_output=True,
                        text=True,
                        encoding='utf-8',
                        errors='replace'
                    )

                    ir_output = result.stdout + result.stderr

                    if result.returncode != 0:
                        error_msg = f"Compilation failed with exit code {result.returncode}"
                        if result.stderr:
                            error_msg += f"\nStderr: {result.stderr[:500]}"
                        if self.show_details:
                            print(color_text(f"  Debug: stdout len={len(result.stdout)}, stderr len={len(result.stderr)}", Colors.YELLOW))
                        return TestResult(
                            success=False,
                            stage='Compilation',
                            error=error_msg,
                            ir_output=ir_output
                        )

                elif cmd.endswith('.exe'):
                    # Runtime execution
                    if self.show_details:
                        print(color_text(f"  Running: {cmd}", Colors.CYAN))

                    result = subprocess.run(
                        cmd,
                        shell=True,
                        capture_output=True,
                        text=True,
                        encoding='utf-8',
                        errors='replace'
                    )

                    runtime_output = result.stdout
                    exit_code = result.returncode

        except Exception as e:
            return TestResult(
                success=False,
                stage='Execution',
                error=str(e)
            )

        return TestResult(
            success=True,
            ir_output=ir_output,
            runtime_output=runtime_output.strip(),
            exit_code=exit_code
        )

    def test_check_patterns(self, ir_output: str, patterns: List[CheckPattern]) -> TestResult:
        """Validate CHECK patterns against IR output."""
        ir_lines = [line.strip() for line in ir_output.split('\n')]
        current_pos = 0
        failures = []

        for pattern in patterns:
            # Convert pattern to regex
            regex_pattern = pattern.pattern
            regex_pattern = regex_pattern.replace('{{', '(?:')
            regex_pattern = regex_pattern.replace('}}', ')')
            # Don't replace .* - it's already a regex pattern

            try:
                regex = re.compile(regex_pattern)
            except re.error as e:
                failures.append(f"Line {pattern.line}: Invalid regex pattern '{pattern.pattern}': {e}")
                continue

            if pattern.type == CheckType.CHECK:
                # Find pattern starting from current position
                found = False
                for i in range(current_pos, len(ir_lines)):
                    if regex.search(ir_lines[i]):
                        current_pos = i + 1
                        found = True
                        break

                if not found:
                    failures.append(f"Line {pattern.line} : CHECK pattern not found: {pattern.pattern}")

            elif pattern.type == CheckType.CHECK_NOT:
                # Pattern must NOT appear anywhere from current position onward
                for i in range(current_pos, len(ir_lines)):
                    if regex.search(ir_lines[i]):
                        failures.append(
                            f"Line {pattern.line} : CHECK-NOT pattern should not appear: {pattern.pattern}"
                        )
                        break

        return TestResult(
            success=len(failures) == 0,
            stage='CHECK Patterns',
            failures=failures
        )

    def test_output(self, actual: str, expected: List[str], expected_exit: int, actual_exit: int) -> TestResult:
        """Validate runtime output and exit code."""
        failures = []

        if actual_exit != expected_exit:
            failures.append(f"Exit code mismatch: expected {expected_exit}, got {actual_exit}")

        if expected:
            expected_str = '\n'.join(expected)
            # Normalize line endings
            actual_normalized = actual.replace('\r\n', '\n').replace('\r', '\n').rstrip()

            if actual_normalized != expected_str:
                failures.append(f"Output mismatch: expected '{expected_str}', got '{actual_normalized}'")

        return TestResult(
            success=len(failures) == 0,
            stage='Output',
            failures=failures
        )

    def run_single_test(self, test_file: Path):
        """Run a single test file."""
        self.total_tests += 1

        test_name = str(test_file.relative_to(self.script_dir))
        print(color_text(f"[{self.total_tests}] Testing: {test_name}", Colors.BLUE))

        test = self.parse_test_file(test_file)

        # Show XFAIL reason if present
        if test.expected_failure:
            print(color_text(f"  [XFAIL: {test.expected_failure_reason}]", Colors.YELLOW))

        # Create temporary directory
        temp_dir = Path(tempfile.gettempdir()) / f"golden_ir_{uuid.uuid4()}"
        temp_dir.mkdir(parents=True, exist_ok=True)

        try:
            # Compile and run
            result = self.run_test_compilation(test, temp_dir)

            if not result.success:
                print(color_text('  X COMPILATION FAILED', Colors.RED))
                print(color_text(f"  Error: {result.error}", Colors.RED))
                
                # Check if this was expected to fail
                if test.expected_failure:
                    print(color_text('  XFAIL (Expected failure)', Colors.YELLOW))
                    self.xfail_tests += 1
                else:
                    self.failed_tests += 1
                    self.failure_details.append({
                        'test': test_name,
                        'stage': result.stage,
                        'error': result.error
                    })
                return

            # Check patterns
            if test.check_patterns:
                check_result = self.test_check_patterns(result.ir_output, test.check_patterns)

                if not check_result.success:
                    print(color_text('  X CHECK PATTERNS FAILED', Colors.RED))
                    for failure in check_result.failures:
                        print(color_text(f"    {failure}", Colors.RED))

                    # Check if this was expected to fail
                    if test.expected_failure:
                        print(color_text('  XFAIL (Expected failure)', Colors.YELLOW))
                        self.xfail_tests += 1
                    else:
                        self.failed_tests += 1
                        self.failure_details.append({
                            'test': test_name,
                            'stage': 'CHECK Patterns',
                            'failures': check_result.failures
                        })
                    return

            # Check output
            if test.expected_output or test.expected_exit_code != 0:
                output_result = self.test_output(
                    result.runtime_output,
                    test.expected_output,
                    test.expected_exit_code,
                    result.exit_code
                )

                if not output_result.success:
                    print(color_text('  X OUTPUT VALIDATION FAILED', Colors.RED))
                    for failure in output_result.failures:
                        print(color_text(f"    {failure}", Colors.RED))

                    # Check if this was expected to fail
                    if test.expected_failure:
                        print(color_text('  XFAIL (Expected failure)', Colors.YELLOW))
                        self.xfail_tests += 1
                    else:
                        self.failed_tests += 1
                        self.failure_details.append({
                            'test': test_name,
                            'stage': 'Output',
                            'failures': output_result.failures
                        })
                    return

            # Test passed
            if test.expected_failure:
                # Expected failure passed - this is a good surprise!
                print(color_text('  ! XPASS (Unexpected pass - bug fixed?)', Colors.YELLOW))
                self.xpass_tests += 1
            else:
                print(color_text('  + PASSED', Colors.GREEN))
                self.passed_tests += 1

        finally:
            # Clean up temp directory
            if temp_dir.exists():
                import shutil
                shutil.rmtree(temp_dir, ignore_errors=True)

    def show_summary(self):
        """Display test summary."""
        print()
        print('=' * 60)
        print('Golden IR Test Summary')
        print('=' * 60)

        print(f"Total Tests: {self.total_tests}")
        print(color_text(f"Passed: {self.passed_tests}", Colors.GREEN))

        if self.failed_tests > 0:
            print(color_text(f"Failed: {self.failed_tests}", Colors.RED))
        else:
            print("Failed: 0")

        if self.xfail_tests > 0:
            print(color_text(f"XFail: {self.xfail_tests} (Expected failures)", Colors.YELLOW))

        if self.xpass_tests > 0:
            print(color_text(f"XPass: {self.xpass_tests} (Unexpected passes - bugs fixed!)", Colors.YELLOW))

        pass_rate = (self.passed_tests / self.total_tests * 100) if self.total_tests > 0 else 0
        color = Colors.GREEN if pass_rate == 100 else Colors.YELLOW
        print(color_text(f"Pass Rate: {pass_rate:.1f}%", color))

        print('=' * 60)

    def run(self) -> int:
        """Main entry point - run all tests."""
        if not self.check_compiler_exists():
            return 1

        test_files = self.get_test_files()

        print()
        print(color_text('Golden IR Test Runner', Colors.CYAN))
        print(color_text(f"Running {len(test_files)} tests.", Colors.CYAN))
        print()

        for test_file in test_files:
            self.run_single_test(test_file)

        self.show_summary()

        return 0 if self.failed_tests == 0 else 1


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description='Golden IR Test Runner')
    parser.add_argument('test_path', help='Path to test file or directory')
    parser.add_argument('--details', action='store_true', help='Show detailed output')
    parser.add_argument('--update-golden', action='store_true', help='Update golden files (not implemented)')

    args = parser.parse_args()

    runner = GoldenIRRunner(args.test_path, show_details=args.details)
    sys.exit(runner.run())


if __name__ == '__main__':
    main()
