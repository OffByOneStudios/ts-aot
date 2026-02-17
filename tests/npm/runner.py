#!/usr/bin/env python3
"""npm Package Test Runner - Tests ts-aot compilation of real npm packages.

For each test directory under tests/npm/:
1. Runs `npm install` if node_modules/ is missing
2. Compiles test_*.ts with ts-aot
3. Runs the resulting executable
4. Reports pass/fail with baseline tracking
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from dataclasses import dataclass
from typing import Dict, List


@dataclass
class TestResult:
    """Result of a single npm package test."""
    name: str
    passed: bool
    install_error: bool = False
    compile_error: bool = False
    runtime_error: bool = False
    exit_code: int = 0
    output: str = ""
    error: str = ""
    phase: str = ""  # "install", "compile", "run", "pass"


class Colors:
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    CYAN = '\033[96m'
    RESET = '\033[0m'
    BOLD = '\033[1m'
    DIM = '\033[2m'

    @staticmethod
    def enabled():
        return sys.stdout.isatty() and os.name != 'nt' or os.environ.get('TERM')


def color(text: str, c: str) -> str:
    if Colors.enabled():
        return f"{c}{text}{Colors.RESET}"
    return text


class NpmTestRunner:
    BASELINE_FILE = '.npm_test_baseline.json'

    def __init__(self, verbose: bool = False, reinstall: bool = False, filter_name: str = None):
        self.script_dir = Path(__file__).parent
        self.root_dir = self.script_dir.parent.parent
        self.compiler_path = self.root_dir / 'build' / 'src' / 'compiler' / 'Release' / 'ts-aot.exe'
        self.verbose = verbose
        self.reinstall = reinstall
        self.filter_name = filter_name

        self.results: List[TestResult] = []
        self.test_statuses: Dict[str, str] = {}
        self.previous_baseline: Dict[str, str] = {}
        self.regressions: List[str] = []
        self.fixes: List[str] = []

    def find_test_dirs(self) -> List[Path]:
        """Find all npm test directories (those containing package.json)."""
        test_dirs = []
        for entry in sorted(self.script_dir.iterdir()):
            if entry.is_dir() and not entry.name.startswith('.'):
                if (entry / 'package.json').exists():
                    if self.filter_name is None or self.filter_name in entry.name:
                        test_dirs.append(entry)
        return test_dirs

    def find_test_file(self, test_dir: Path) -> Path | None:
        """Find the test_*.ts file in a test directory."""
        for f in sorted(test_dir.glob('test_*.ts')):
            return f
        for f in sorted(test_dir.glob('test_*.js')):
            return f
        return None

    def npm_install(self, test_dir: Path) -> tuple[bool, str]:
        """Run npm install in the test directory."""
        node_modules = test_dir / 'node_modules'
        if node_modules.exists() and not self.reinstall:
            return True, ""

        try:
            result = subprocess.run(
                ['npm', 'install', '--no-package-lock', '--no-audit', '--no-fund'],
                cwd=str(test_dir),
                capture_output=True,
                timeout=60,
                encoding='utf-8',
                errors='replace',
                shell=True
            )
            if result.returncode != 0:
                return False, result.stderr or result.stdout or "npm install failed"
            return True, ""
        except subprocess.TimeoutExpired:
            return False, "npm install timeout (60s)"
        except Exception as e:
            return False, str(e)

    def compile_test(self, test_file: Path) -> tuple[bool, str]:
        """Compile a test file with ts-aot."""
        exe_path = test_file.with_suffix('.exe')

        try:
            result = subprocess.run(
                [str(self.compiler_path), str(test_file), '-o', str(exe_path)],
                capture_output=True,
                timeout=60,
                encoding='utf-8',
                errors='replace'
            )
            if result.returncode != 0:
                return False, result.stderr or result.stdout or "Compilation failed"
            return True, ""
        except subprocess.TimeoutExpired:
            return False, "Compilation timeout (60s)"
        except Exception as e:
            return False, str(e)

    def run_test(self, exe_path: Path) -> tuple[int, str, str]:
        """Run a compiled test executable."""
        try:
            result = subprocess.run(
                [str(exe_path)],
                capture_output=True,
                timeout=15,
                encoding='utf-8',
                errors='replace'
            )
            return result.returncode, result.stdout or "", result.stderr or ""
        except subprocess.TimeoutExpired:
            return -1, "", "Execution timeout (15s)"
        except Exception as e:
            return -1, "", str(e)

    def run_single_test(self, test_dir: Path) -> TestResult:
        """Run a single npm package test through install -> compile -> run."""
        name = test_dir.name

        # Find test file
        test_file = self.find_test_file(test_dir)
        if not test_file:
            return TestResult(name=name, passed=False, error="No test_*.ts file found",
                              phase="setup")

        # Phase 1: npm install
        if self.verbose:
            print(f"  [{name}] npm install...")
        ok, err = self.npm_install(test_dir)
        if not ok:
            return TestResult(name=name, passed=False, install_error=True,
                              error=err, phase="install")

        # Phase 2: Compile
        if self.verbose:
            print(f"  [{name}] compiling {test_file.name}...")
        ok, err = self.compile_test(test_file)
        if not ok:
            return TestResult(name=name, passed=False, compile_error=True,
                              error=err, phase="compile")

        # Phase 3: Run
        exe_path = test_file.with_suffix('.exe')
        if self.verbose:
            print(f"  [{name}] running...")
        exit_code, output, error = self.run_test(exe_path)

        # Clean up exe
        if exe_path.exists():
            try:
                exe_path.unlink()
            except:
                pass

        # Check both exit code and output for FAIL patterns
        has_fail_output = 'FAIL:' in output or 'test(s) failed' in output
        passed = exit_code == 0 and not has_fail_output
        phase = "pass" if passed else "run"

        return TestResult(
            name=name, passed=passed, runtime_error=not passed,
            exit_code=exit_code, output=output, error=error, phase=phase
        )

    def print_result(self, result: TestResult):
        """Print a single test result."""
        w = 12
        if result.install_error:
            status = color("INSTALL ERR".ljust(w), Colors.RED)
        elif result.compile_error:
            status = color("COMPILE ERR".ljust(w), Colors.RED)
        elif result.runtime_error:
            status = color("RUNTIME ERR".ljust(w), Colors.RED)
        elif result.passed:
            status = color("PASS".ljust(w), Colors.GREEN)
        else:
            status = color("FAIL".ljust(w), Colors.RED)

        print(f"  {status} {result.name}")

        if not result.passed or self.verbose:
            if result.error:
                # Truncate long errors
                err_lines = result.error.strip().split('\n')
                for line in err_lines[:10]:
                    print(f"    {color(line, Colors.DIM)}")
                if len(err_lines) > 10:
                    print(f"    {color(f'... ({len(err_lines) - 10} more lines)', Colors.DIM)}")
            if result.output and self.verbose:
                for line in result.output.strip().split('\n')[-10:]:
                    print(f"    {line}")

    def classify(self, result: TestResult) -> str:
        if result.install_error:
            return 'install_error'
        elif result.compile_error:
            return 'compile_error'
        elif result.runtime_error:
            return 'runtime_error'
        elif result.passed:
            return 'pass'
        return 'fail'

    def _baseline_path(self) -> Path:
        return self.script_dir / self.BASELINE_FILE

    def _load_baseline(self):
        path = self._baseline_path()
        if path.exists():
            try:
                with open(path, 'r', encoding='utf-8') as f:
                    self.previous_baseline = json.load(f)
            except (json.JSONDecodeError, OSError):
                self.previous_baseline = {}

    def _save_baseline(self):
        path = self._baseline_path()
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(self.test_statuses, f, indent=2, sort_keys=True)

    def _compare_baseline(self):
        if not self.previous_baseline:
            return
        for name, current in self.test_statuses.items():
            prev = self.previous_baseline.get(name)
            if prev is None:
                continue
            if prev == 'pass' and current != 'pass':
                self.regressions.append(name)
            elif prev != 'pass' and current == 'pass':
                self.fixes.append(name)

    def run_all(self) -> int:
        if not self.compiler_path.exists():
            print(color(f"ERROR: Compiler not found at {self.compiler_path}", Colors.RED))
            return 1

        self._load_baseline()

        print(color("\n=== npm Package Test Suite ===\n", Colors.BOLD))

        test_dirs = self.find_test_dirs()
        if not test_dirs:
            print(color("No npm test directories found!", Colors.RED))
            return 1

        print(f"Found {len(test_dirs)} package test(s)\n")

        total = passed = failed = 0
        install_errors = compile_errors = runtime_errors = 0

        for test_dir in test_dirs:
            result = self.run_single_test(test_dir)
            self.results.append(result)
            self.print_result(result)

            self.test_statuses[result.name] = self.classify(result)

            total += 1
            if result.passed:
                passed += 1
            else:
                failed += 1
                if result.install_error:
                    install_errors += 1
                elif result.compile_error:
                    compile_errors += 1
                elif result.runtime_error:
                    runtime_errors += 1

        self._compare_baseline()
        self._save_baseline()

        # Summary
        print(color("\n=== Summary ===\n", Colors.BOLD))
        print(f"Total:           {total}")
        print(color(f"Passed:          {passed}", Colors.GREEN))
        print(color(f"Failed:          {failed}", Colors.RED))
        if install_errors:
            print(color(f"  Install errs:  {install_errors}", Colors.RED))
        if compile_errors:
            print(color(f"  Compile errs:  {compile_errors}", Colors.RED))
        if runtime_errors:
            print(color(f"  Runtime errs:  {runtime_errors}", Colors.RED))

        if self.previous_baseline:
            prev_pass = sum(1 for s in self.previous_baseline.values() if s == 'pass')
            print(f"\nPrevious:        {prev_pass}/{len(self.previous_baseline)} passed")

            if self.regressions:
                print(color(f"\nREGRESSIONS ({len(self.regressions)}):", Colors.RED))
                for n in sorted(self.regressions):
                    p = self.previous_baseline.get(n, '?')
                    c = self.test_statuses.get(n, '?')
                    print(color(f"  - {n}  ({p} -> {c})", Colors.RED))

            if self.fixes:
                print(color(f"\nFixes ({len(self.fixes)}):", Colors.GREEN))
                for n in sorted(self.fixes):
                    p = self.previous_baseline.get(n, '?')
                    c = self.test_statuses.get(n, '?')
                    print(color(f"  + {n}  ({p} -> {c})", Colors.GREEN))

            if not self.regressions and not self.fixes:
                print(color("No regressions.", Colors.GREEN))
        else:
            print(color("\nNo previous baseline. This run saved as baseline.", Colors.YELLOW))

        return 0 if failed == 0 else 1


def main():
    parser = argparse.ArgumentParser(description='Run npm package integration tests')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    parser.add_argument('--reinstall', action='store_true', help='Force npm reinstall')
    parser.add_argument('--filter', type=str, default=None, help='Filter tests by name')
    args = parser.parse_args()

    runner = NpmTestRunner(verbose=args.verbose, reinstall=args.reinstall,
                           filter_name=args.filter)
    return runner.run_all()


if __name__ == '__main__':
    sys.exit(main())
