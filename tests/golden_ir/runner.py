#!/usr/bin/env python3
"""Golden Test Runner - Validates HIR and LLVM IR output against CHECK patterns.

Supports two types of CHECK patterns:
- CHECK, CHECK-NOT, CHECK-NEXT, CHECK-DAG: Validate LLVM IR output (--dump-ir)
- HIR-CHECK, HIR-CHECK-NOT, HIR-CHECK-NEXT, HIR-CHECK-DAG: Validate HIR output (--dump-hir)

Usage:
    python tests/golden_ir/runner.py tests/golden_ir/    # LLVM IR tests
    python tests/golden_ir/runner.py tests/golden_hir/   # HIR tests
    python tests/golden_ir/runner.py tests/              # All tests
"""

import argparse
import json
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


class HIRCheckType(Enum):
    """Type of HIR-CHECK pattern for HIR validation."""
    CHECK = "HIR-CHECK"
    CHECK_NOT = "HIR-CHECK-NOT"
    CHECK_NEXT = "HIR-CHECK-NEXT"
    CHECK_DAG = "HIR-CHECK-DAG"


@dataclass
class CheckPattern:
    """A single CHECK pattern from a test file."""
    type: CheckType
    pattern: str
    line: int


@dataclass
class HIRCheckPattern:
    """A single HIR-CHECK pattern from a test file."""
    type: HIRCheckType
    pattern: str
    line: int


@dataclass
class TestCase:
    """Represents a single test file and its expectations."""
    file_path: Path
    file_name: str
    run_command: Optional[str] = None
    check_patterns: List[CheckPattern] = field(default_factory=list)
    hir_check_patterns: List[HIRCheckPattern] = field(default_factory=list)
    expected_output: List[str] = field(default_factory=list)
    expected_exit_code: int = 0


@dataclass
class TestResult:
    """Result of running a test."""
    success: bool
    stage: str = ""
    error: Optional[str] = None
    failures: List[str] = field(default_factory=list)
    ir_output: str = ""
    hir_output: str = ""
    runtime_output: str = ""
    exit_code: int = 0


def extract_hir_section(output: str) -> str:
    """Extract just the HIR module section from compiler output.

    The compiler may output additional information like linker arguments
    before or after the HIR dump. This function extracts just the HIR
    module portion which starts with '; HIR Module:' or 'define @'.
    """
    lines = output.split('\n')
    hir_start = -1
    hir_end = len(lines)

    # Find the start of HIR output
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('; HIR Module:') or stripped.startswith('define @'):
            hir_start = i
            break

    if hir_start < 0:
        # No HIR module found - return original output for error diagnosis
        return output

    # Find the end of HIR output (next section or end)
    # HIR output ends when we see linker output, blank lines followed by non-HIR content, etc.
    in_function = False
    brace_depth = 0

    for i in range(hir_start, len(lines)):
        line = lines[i].strip()

        # Track function depth (define ... { ... })
        if 'define @' in line:
            in_function = True
        if '{' in line:
            brace_depth += line.count('{')
        if '}' in line:
            brace_depth -= line.count('}')
            if brace_depth <= 0:
                in_function = False

        # Stop if we hit linker output or other non-HIR content
        if line.startswith('Linker arguments:') or line.startswith('ts-aot-linker'):
            hir_end = i
            break

    return '\n'.join(lines[hir_start:hir_end])


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

    BASELINE_FILE = '.golden_ir_baseline.json'

    def __init__(self, test_path: str, show_details: bool = False):
        self.script_dir = Path(__file__).parent
        self.root_dir = self.script_dir.parent.parent
        self.tests_dir = self.script_dir.parent  # tests/ directory (parent of golden_ir/)
        self.compiler_path = self.root_dir / 'build' / 'src' / 'compiler' / 'Release' / 'ts-aot.exe'
        self.test_path = test_path
        self.show_details = show_details

        self.total_tests = 0
        self.passed_tests = 0
        self.failed_tests = 0
        self.failure_details = []

        # Per-test results for baseline tracking: {test_name: "pass"|"fail"}
        self.test_results: Dict[str, str] = {}
        self.previous_baseline: Dict[str, str] = {}
        self.regressions: List[str] = []
        self.fixes: List[str] = []

    def check_compiler_exists(self) -> bool:
        """Verify that the compiler exists."""
        if not self.compiler_path.exists():
            print(color_text(f"ERROR: Compiler not found at {self.compiler_path}", Colors.RED))
            print(color_text("Run: cmake --build build --config Release", Colors.YELLOW))
            return False
        return True

    def get_test_files(self) -> List[Path]:
        """Get list of test files to run."""
        # Try as absolute path first
        full_path = Path(self.test_path)
        if not full_path.is_absolute():
            # Try relative to tests/ directory
            if (self.tests_dir / self.test_path).exists():
                full_path = self.tests_dir / self.test_path
            # Try relative to golden_ir/ directory (backwards compatibility)
            elif (self.script_dir / self.test_path).exists():
                full_path = self.script_dir / self.test_path
            # Try relative to current working directory
            elif Path(self.test_path).exists():
                full_path = Path(self.test_path).resolve()
            else:
                print(color_text(f"ERROR: Test path not found: {self.test_path}", Colors.RED))
                sys.exit(1)

        if full_path.is_file():
            return [full_path]
        elif full_path.is_dir():
            # Recursively find all .ts and .js files (excluding .d.ts declaration files)
            test_files = []
            for ext in ['*.ts', '*.js']:
                test_files.extend(f for f in full_path.rglob(ext) if not f.name.endswith('.d.ts'))
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

            # HIR-CHECK patterns (must be before CHECK patterns to match correctly)
            elif match := re.match(r'^\s*//\s*HIR-CHECK(-NEXT|-NOT|-DAG)?:\s*(.+)$', line):
                check_suffix = match.group(1)
                pattern = match.group(2).strip()

                if check_suffix == '-NOT':
                    hir_check_type = HIRCheckType.CHECK_NOT
                elif check_suffix == '-NEXT':
                    hir_check_type = HIRCheckType.CHECK_NEXT
                elif check_suffix == '-DAG':
                    hir_check_type = HIRCheckType.CHECK_DAG
                else:
                    hir_check_type = HIRCheckType.CHECK

                test.hir_check_patterns.append(HIRCheckPattern(
                    type=hir_check_type,
                    pattern=pattern,
                    line=line_num
                ))

            # CHECK patterns (LLVM IR)
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
        hir_output = ""
        runtime_output = ""
        exit_code = 0

        try:
            # If HIR-CHECK patterns exist, first run with --dump-hir to capture HIR output
            if test.hir_check_patterns:
                # Build HIR dump command (compile-only, no execution)
                # Note: --use-hir is required to enable the HIR pipeline
                hir_cmd = f"{self.compiler_path} {test.file_path} --use-hir --dump-hir -o {exe_path}"
                if self.show_details:
                    print(color_text(f"  Running HIR dump: {hir_cmd}", Colors.CYAN))

                result = subprocess.run(
                    hir_cmd,
                    shell=True,
                    capture_output=True,
                    text=True,
                    encoding='utf-8',
                    errors='replace'
                )

                raw_hir_output = result.stdout + result.stderr
                # Extract just the HIR section, filtering out linker arguments etc.
                hir_output = extract_hir_section(raw_hir_output)

                if self.show_details:
                    print(color_text(f"  Raw HIR output length: {len(raw_hir_output)} chars", Colors.YELLOW))
                    print(color_text(f"  Filtered HIR output length: {len(hir_output)} chars", Colors.YELLOW))
                    if len(hir_output) > 0:
                        print(color_text(f"  HIR output preview: {hir_output[:500]}", Colors.YELLOW))

                if result.returncode != 0:
                    error_msg = f"HIR dump failed with exit code {result.returncode}"
                    if result.stderr:
                        error_msg += f"\nStderr: {result.stderr[:500]}"
                    return TestResult(
                        success=False,
                        stage='HIR Dump',
                        error=error_msg,
                        hir_output=hir_output
                    )

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
                            ir_output=ir_output,
                            hir_output=hir_output
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
            hir_output=hir_output,
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
            # First, escape all regex special characters EXCEPT our {{...}} placeholders
            regex_pattern = pattern.pattern

            # Temporarily replace {{...}} with unique tokens
            placeholders = []
            placeholder_re = re.compile(r'\{\{.*?\}\}')
            def save_placeholder(match):
                placeholders.append(match.group(0))
                return f'\x00PLACEHOLDER{len(placeholders)-1}\x00'
            regex_pattern = placeholder_re.sub(save_placeholder, regex_pattern)

            # Escape regex special chars in the rest of the pattern
            regex_pattern = re.escape(regex_pattern)

            # Restore placeholders and convert to regex groups
            for i, ph in enumerate(placeholders):
                inner = ph[2:-2]  # Remove {{ and }}
                # Use actual null byte (\x00) not escaped string (\\x00)
                regex_pattern = regex_pattern.replace(f'\x00PLACEHOLDER{i}\x00', f'(?:{inner})')

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

    def test_hir_check_patterns(self, hir_output: str, patterns: List[HIRCheckPattern]) -> TestResult:
        """Validate HIR-CHECK patterns against HIR output."""
        hir_lines = [line.strip() for line in hir_output.split('\n')]
        current_pos = 0
        failures = []
        last_match_pos = -1

        for pattern in patterns:
            # Convert pattern to regex
            # First, escape all regex special characters EXCEPT our {{...}} placeholders
            # Save {{...}} placeholders, escape the rest, then restore placeholders
            regex_pattern = pattern.pattern

            # Temporarily replace {{...}} with unique tokens
            placeholders = []
            placeholder_re = re.compile(r'\{\{.*?\}\}')
            def save_placeholder(match):
                placeholders.append(match.group(0))
                return f'\x00PLACEHOLDER{len(placeholders)-1}\x00'
            regex_pattern = placeholder_re.sub(save_placeholder, regex_pattern)

            # Escape regex special chars in the rest of the pattern
            regex_pattern = re.escape(regex_pattern)

            # Restore placeholders and convert to regex groups
            for i, ph in enumerate(placeholders):
                inner = ph[2:-2]  # Remove {{ and }}
                # Use actual null byte (\x00) not escaped string (\\x00)
                regex_pattern = regex_pattern.replace(f'\x00PLACEHOLDER{i}\x00', f'(?:{inner})')

            try:
                regex = re.compile(regex_pattern)
            except re.error as e:
                failures.append(f"Line {pattern.line}: Invalid regex pattern '{pattern.pattern}': {e}")
                continue

            if pattern.type == HIRCheckType.CHECK:
                # Find pattern starting from current position
                found = False
                for i in range(current_pos, len(hir_lines)):
                    if regex.search(hir_lines[i]):
                        current_pos = i + 1
                        last_match_pos = i
                        found = True
                        break

                if not found:
                    failures.append(f"Line {pattern.line}: HIR-CHECK pattern not found: {pattern.pattern}")

            elif pattern.type == HIRCheckType.CHECK_NOT:
                # Pattern must NOT appear anywhere from current position onward
                for i in range(current_pos, len(hir_lines)):
                    if regex.search(hir_lines[i]):
                        failures.append(
                            f"Line {pattern.line}: HIR-CHECK-NOT pattern should not appear: {pattern.pattern}"
                        )
                        break

            elif pattern.type == HIRCheckType.CHECK_NEXT:
                # Pattern must appear on the next non-empty line after last match
                if last_match_pos < 0:
                    failures.append(f"Line {pattern.line}: HIR-CHECK-NEXT without preceding match")
                    continue

                next_pos = last_match_pos + 1
                # Skip empty lines
                while next_pos < len(hir_lines) and not hir_lines[next_pos].strip():
                    next_pos += 1

                if next_pos >= len(hir_lines):
                    failures.append(f"Line {pattern.line}: HIR-CHECK-NEXT pattern not found (no more lines): {pattern.pattern}")
                elif not regex.search(hir_lines[next_pos]):
                    failures.append(f"Line {pattern.line}: HIR-CHECK-NEXT pattern not on next line: {pattern.pattern}")
                    failures.append(f"  Actual line: {hir_lines[next_pos]}")
                else:
                    current_pos = next_pos + 1
                    last_match_pos = next_pos

            elif pattern.type == HIRCheckType.CHECK_DAG:
                # Pattern may appear anywhere (unordered)
                found = False
                for i in range(len(hir_lines)):
                    if regex.search(hir_lines[i]):
                        found = True
                        break

                if not found:
                    failures.append(f"Line {pattern.line}: HIR-CHECK-DAG pattern not found: {pattern.pattern}")

        return TestResult(
            success=len(failures) == 0,
            stage='HIR-CHECK Patterns',
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

        # Try to make path relative for display
        try:
            test_name = str(test_file.relative_to(self.tests_dir))
        except ValueError:
            try:
                test_name = str(test_file.relative_to(self.root_dir))
            except ValueError:
                test_name = str(test_file)
        # Normalize path separators for consistent baseline keys
        test_name = test_name.replace('\\', '/')

        print(color_text(f"[{self.total_tests}] Testing: {test_name}", Colors.BLUE))

        test = self.parse_test_file(test_file)

        # Create temporary directory
        temp_dir = Path(tempfile.gettempdir()) / f"golden_ir_{uuid.uuid4()}"
        temp_dir.mkdir(parents=True, exist_ok=True)

        try:
            # Compile and run
            result = self.run_test_compilation(test, temp_dir)

            if not result.success:
                print(color_text('  X COMPILATION FAILED', Colors.RED))
                print(color_text(f"  Error: {result.error}", Colors.RED))
                self.failed_tests += 1
                self.failure_details.append({
                    'test': test_name,
                    'stage': result.stage,
                    'error': result.error
                })
                self.test_results[test_name] = 'fail'
                return

            # Check HIR patterns first (if any)
            if test.hir_check_patterns:
                hir_check_result = self.test_hir_check_patterns(result.hir_output, test.hir_check_patterns)

                if not hir_check_result.success:
                    print(color_text('  X HIR-CHECK PATTERNS FAILED', Colors.RED))
                    for failure in hir_check_result.failures:
                        print(color_text(f"    {failure}", Colors.RED))
                    self.failed_tests += 1
                    self.failure_details.append({
                        'test': test_name,
                        'stage': 'HIR-CHECK Patterns',
                        'failures': hir_check_result.failures
                    })
                    self.test_results[test_name] = 'fail'
                    return

            # Check LLVM IR patterns
            if test.check_patterns:
                check_result = self.test_check_patterns(result.ir_output, test.check_patterns)

                if not check_result.success:
                    print(color_text('  X CHECK PATTERNS FAILED', Colors.RED))
                    for failure in check_result.failures:
                        print(color_text(f"    {failure}", Colors.RED))
                    self.failed_tests += 1
                    self.failure_details.append({
                        'test': test_name,
                        'stage': 'CHECK Patterns',
                        'failures': check_result.failures
                    })
                    self.test_results[test_name] = 'fail'
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
                    self.failed_tests += 1
                    self.failure_details.append({
                        'test': test_name,
                        'stage': 'Output',
                        'failures': output_result.failures
                    })
                    self.test_results[test_name] = 'fail'
                    return

            # Test passed
            print(color_text('  + PASSED', Colors.GREEN))
            self.passed_tests += 1
            self.test_results[test_name] = 'pass'

        finally:
            # Clean up temp directory
            if temp_dir.exists():
                import shutil
                shutil.rmtree(temp_dir, ignore_errors=True)

    def _baseline_path(self) -> Path:
        """Return the path to the baseline JSON file."""
        return self.script_dir / self.BASELINE_FILE

    def _load_baseline(self):
        """Load the previous test baseline if it exists."""
        path = self._baseline_path()
        if path.exists():
            try:
                with open(path, 'r', encoding='utf-8') as f:
                    self.previous_baseline = json.load(f)
            except (json.JSONDecodeError, OSError):
                self.previous_baseline = {}

    def _save_baseline(self):
        """Save the current test results as the new baseline."""
        path = self._baseline_path()
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(self.test_results, f, indent=2, sort_keys=True)

    def _compare_baseline(self):
        """Compare current results against previous baseline to find regressions and fixes."""
        if not self.previous_baseline:
            return

        for test_name, current_status in self.test_results.items():
            prev_status = self.previous_baseline.get(test_name)
            if prev_status is None:
                continue  # New test, no comparison
            if prev_status == 'pass' and current_status == 'fail':
                self.regressions.append(test_name)
            elif prev_status == 'fail' and current_status == 'pass':
                self.fixes.append(test_name)

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

        pass_rate = (self.passed_tests / self.total_tests * 100) if self.total_tests > 0 else 0
        color = Colors.GREEN if pass_rate == 100 else Colors.YELLOW
        print(color_text(f"Pass Rate: {pass_rate:.1f}%", color))

        # Show regression/fix comparison against baseline
        if self.previous_baseline:
            prev_pass = sum(1 for s in self.previous_baseline.values() if s == 'pass')
            prev_total = len(self.previous_baseline)
            print(f"Previous:   {prev_pass}/{prev_total} passed")

            if self.regressions:
                print()
                print(color_text(f"REGRESSIONS ({len(self.regressions)}):", Colors.RED))
                for name in sorted(self.regressions):
                    print(color_text(f"  - {name}", Colors.RED))

            if self.fixes:
                print()
                print(color_text(f"Fixes ({len(self.fixes)}):", Colors.GREEN))
                for name in sorted(self.fixes):
                    print(color_text(f"  + {name}", Colors.GREEN))

            if not self.regressions and not self.fixes:
                print(color_text("No regressions.", Colors.GREEN))
        else:
            print(color_text("No previous baseline found. This run will be saved as the baseline.", Colors.YELLOW))

        print('=' * 60)

    def run(self) -> int:
        """Main entry point - run all tests."""
        if not self.check_compiler_exists():
            return 1

        test_files = self.get_test_files()

        # Load previous baseline for comparison
        self._load_baseline()

        print()
        print(color_text('Golden IR Test Runner', Colors.CYAN))
        print(color_text(f"Running {len(test_files)} tests.", Colors.CYAN))
        print()

        for test_file in test_files:
            self.run_single_test(test_file)

        # Compare against baseline and save new results
        self._compare_baseline()
        self._save_baseline()

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
