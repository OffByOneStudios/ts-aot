#!/usr/bin/env python3
"""Unified test runner for ts-aot - runs all test suites from a single entry point.

Usage:
    python tests/run_all.py                    # Run all 5 suites
    python tests/run_all.py --suite golden-ir  # Run one suite
    python tests/run_all.py --suite node -v    # Verbose single suite
    python tests/run_all.py --parallel         # Run suites concurrently
    python tests/run_all.py --list             # List available suites
"""

import argparse
import re
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

TESTS_DIR = Path(__file__).parent
PROJECT_ROOT = TESTS_DIR.parent

# Suite definitions: name, key (for --suite), command components, verbose flag
# `parallel_args` are appended when --fast is specified.
SUITES = [
    {
        'name': 'Golden IR',
        'key': 'golden-ir',
        'script': str(TESTS_DIR / 'golden_ir' / 'runner.py'),
        'args': [str(TESTS_DIR / 'golden_ir')],
        'verbose_flag': '--details',
        'parallel_args': ['-j', '8'],
    },
    {
        'name': 'Golden HIR',
        'key': 'golden-hir',
        'script': str(TESTS_DIR / 'golden_ir' / 'runner.py'),
        'args': [str(TESTS_DIR / 'golden_hir')],
        'verbose_flag': '--details',
        'parallel_args': ['-j', '8'],
    },
    {
        'name': 'Node.js',
        'key': 'node',
        'script': str(TESTS_DIR / 'node' / 'run_tests.py'),
        'args': [],
        'verbose_flag': '-v',
        'parallel_args': ['-j', '8'],
    },
    {
        'name': 'NPM',
        'key': 'npm',
        'script': str(TESTS_DIR / 'npm' / 'runner.py'),
        'args': [],
        'verbose_flag': '-v',
        # npm tests share node_modules — keep sequential
    },
    {
        'name': 'Integration',
        'key': 'integration',
        'script': str(TESTS_DIR / 'integration' / 'run_integration_tests.py'),
        'args': [],
        'verbose_flag': None,
    },
    {
        'name': 'Server',
        'key': 'server',
        'script': str(TESTS_DIR / 'server' / 'run_server_tests.py'),
        'args': [],
        'verbose_flag': '-v',
        'parallel_args': ['-j', '4'],  # capped — each test spawns a server subprocess
    },
    {
        'name': 'test262',
        'key': 'test262',
        'script': str(TESTS_DIR / 'test262' / 'run_test262.py'),
        'args': ['-c', 'language', '--limit', '500'],
        'verbose_flag': '-v',
        'parallel_args': ['-j', '12', '--interleave'],
        'opt_in': True,  # Not run by default — too slow for CI
    },
]

ANSI_RE = re.compile(r'\x1b\[[0-9;]*m')


def strip_ansi(text: str) -> str:
    """Remove ANSI escape codes from text."""
    return ANSI_RE.sub('', text)


def parse_results(output: str, suite_key: str) -> dict:
    """Parse passed/failed counts from a runner's output."""
    clean = strip_ansi(output)
    passed = 0
    failed = 0

    if suite_key in ('golden-ir', 'golden-hir'):
        # Golden runner: "Passed: N" and "Failed: N"
        m = re.search(r'Passed:\s+(\d+)', clean)
        if m:
            passed = int(m.group(1))
        m = re.search(r'Failed:\s+(\d+)', clean)
        if m:
            failed = int(m.group(1))

    elif suite_key == 'node':
        # Node runner: "Passed:            N" and "Failed:            N"
        m = re.search(r'Passed:\s+(\d+)', clean)
        if m:
            passed = int(m.group(1))
        m = re.search(r'Failed:\s+(\d+)', clean)
        if m:
            failed = int(m.group(1))

    elif suite_key == 'npm':
        # NPM runner: "Passed:          N" and "Failed:          N"
        m = re.search(r'Passed:\s+(\d+)', clean)
        if m:
            passed = int(m.group(1))
        m = re.search(r'Failed:\s+(\d+)', clean)
        if m:
            failed = int(m.group(1))

    elif suite_key == 'integration':
        # Integration runner: "Passed: N" and "Failed: N"
        m = re.search(r'Passed:\s+(\d+)', clean)
        if m:
            passed = int(m.group(1))
        m = re.search(r'Failed:\s+(\d+)', clean)
        if m:
            failed = int(m.group(1))

    elif suite_key == 'server':
        # Server runner: "Passed: N" and "Failed: N"
        m = re.search(r'Passed:\s+(\d+)', clean)
        if m:
            passed = int(m.group(1))
        m = re.search(r'Failed:\s+(\d+)', clean)
        if m:
            failed = int(m.group(1))

    elif suite_key == 'test262':
        # test262 runner: "Passed:        N" and "Failed:        N"
        m = re.search(r'Passed:\s+(\d+)', clean)
        if m:
            passed = int(m.group(1))
        # Count all non-pass non-skip as failed
        for label in ('Failed', 'Compile Error', 'Timeout', 'Crash'):
            m = re.search(rf'{label}:\s+(\d+)', clean)
            if m:
                failed += int(m.group(1))

    return {'passed': passed, 'failed': failed}


def run_suite(suite: dict, verbose: bool = False) -> dict:
    """Run a single suite and return results."""
    cmd = [sys.executable, suite['script']] + suite['args']
    if verbose and suite.get('verbose_flag'):
        cmd.append(suite['verbose_flag'])

    start = time.time()
    try:
        if verbose:
            # Stream output in real-time
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding='utf-8',
                errors='replace',
            )
            output_lines = []
            for line in proc.stdout:
                print(line, end='')
                output_lines.append(line)
            proc.wait()
            output = ''.join(output_lines)
            returncode = proc.returncode
        else:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                encoding='utf-8',
                errors='replace',
                timeout=600,
            )
            output = result.stdout + result.stderr
            returncode = result.returncode
    except subprocess.TimeoutExpired:
        output = ''
        returncode = -1
    except Exception as e:
        output = str(e)
        returncode = -1

    elapsed = time.time() - start
    parsed = parse_results(output, suite['key'])

    return {
        'name': suite['name'],
        'key': suite['key'],
        'passed': parsed['passed'],
        'failed': parsed['failed'],
        'total': parsed['passed'] + parsed['failed'],
        'elapsed': elapsed,
        'returncode': returncode,
        'output': output,
    }


def _run_suite_wrapper(args):
    """Wrapper for ProcessPoolExecutor (needs top-level picklable function)."""
    suite, verbose = args
    return run_suite(suite, verbose)


def print_summary(results: list):
    """Print the final summary table."""
    print()
    print('=' * 66)
    print(f"{'Suite':<18} {'Passed':>7} {'Failed':>7} {'Total':>7} {'Rate':>7} {'Time':>8}")
    print('-' * 66)

    total_passed = 0
    total_failed = 0
    total_total = 0
    total_time = 0.0

    for r in results:
        rate = f"{r['passed'] / r['total'] * 100:.0f}%" if r['total'] > 0 else 'N/A'
        time_str = f"{r['elapsed']:.1f}s"
        print(f"{r['name']:<18} {r['passed']:>7} {r['failed']:>7} {r['total']:>7} {rate:>7} {time_str:>8}")
        total_passed += r['passed']
        total_failed += r['failed']
        total_total += r['total']
        total_time += r['elapsed']

    print('-' * 66)
    total_rate = f"{total_passed / total_total * 100:.0f}%" if total_total > 0 else 'N/A'
    total_time_str = f"{total_time:.1f}s"
    print(f"{'TOTAL':<18} {total_passed:>7} {total_failed:>7} {total_total:>7} {total_rate:>7} {total_time_str:>8}")
    print('=' * 66)


def main():
    parser = argparse.ArgumentParser(description='ts-aot unified test runner')
    parser.add_argument('--suite', '-s', type=str, help='Run a specific suite (e.g. golden-ir, node, npm)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Stream sub-runner output in real-time')
    parser.add_argument('--parallel', '-p', action='store_true', help='Run suites concurrently')
    parser.add_argument('--fast', action='store_true',
                        help='Inject per-suite parallelism flags (-j N for each runner)')
    parser.add_argument('--list', '-l', action='store_true', help='List available suites')
    args = parser.parse_args()

    if args.list:
        print("Available test suites:")
        for s in SUITES:
            print(f"  {s['key']:<15} {s['name']}")
        return 0

    # Select suites
    if args.suite:
        selected = [s for s in SUITES if s['key'] == args.suite]
        if not selected:
            print(f"Unknown suite: {args.suite}")
            print(f"Available: {', '.join(s['key'] for s in SUITES)}")
            return 1
    else:
        selected = [s for s in SUITES if not s.get('opt_in')]

    # --fast: append parallel_args to each selected suite's args
    if args.fast:
        selected = [
            {**s, 'args': s['args'] + s.get('parallel_args', [])}
            for s in selected
        ]

    print("ts-aot Test Runner")
    print("=" * 40)
    print()

    results = []

    if args.parallel and len(selected) > 1:
        # Run suites concurrently
        with ProcessPoolExecutor(max_workers=len(selected)) as executor:
            future_map = {}
            for suite in selected:
                future = executor.submit(run_suite, suite, False)
                future_map[future] = suite

            for future in as_completed(future_map):
                r = future.result()
                status = 'PASS' if r['failed'] == 0 else 'FAIL'
                print(f"[{status}] {r['name']:<15} {r['passed']}/{r['total']} passed ({r['elapsed']:.1f}s)")
                results.append(r)

        # Sort results back to original order
        key_order = {s['key']: i for i, s in enumerate(selected)}
        results.sort(key=lambda r: key_order.get(r['key'], 999))
    else:
        # Run suites sequentially
        for suite in selected:
            if not args.verbose:
                print(f"[....] {suite['name']}...", end='', flush=True)

            r = run_suite(suite, verbose=args.verbose)
            results.append(r)

            if not args.verbose:
                status = 'DONE' if r['failed'] == 0 else 'FAIL'
                # Clear the line and reprint
                print(f"\r[{status}] {r['name']:<15} {r['passed']}/{r['total']} passed ({r['elapsed']:.1f}s)")

            if r['failed'] > 0 and not args.verbose:
                # Show failed suite output excerpt
                clean = strip_ansi(r['output'])
                # Find lines with FAIL or ERROR
                fail_lines = [l for l in clean.split('\n')
                              if any(w in l.upper() for w in ['FAIL', 'ERROR', 'REGRESSION'])]
                if fail_lines:
                    for line in fail_lines[:10]:
                        print(f"  {line.strip()}")

    print_summary(results)

    all_passed = all(r['failed'] == 0 for r in results)
    return 0 if all_passed else 1


if __name__ == '__main__':
    sys.exit(main())
