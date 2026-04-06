#!/usr/bin/env python3
"""Server test runner for ts-aot - manages long-running server processes.

Compiles TypeScript server applications, starts them as subprocesses,
runs HTTP checks against them, and reports pass/fail.

Usage:
    python tests/server/run_server_tests.py              # Run all server tests
    python tests/server/run_server_tests.py -v            # Verbose output
    python tests/server/run_server_tests.py -t NAME       # Run specific test
    python tests/server/run_server_tests.py --coverage    # Run with coverage collection
"""

import argparse
import atexit
import importlib.util
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ts_test_platform import get_compiler_path, get_exe_suffix

SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
COMPILER = get_compiler_path(PROJECT_ROOT)
BASELINE_FILE = '.server_test_baseline.json'

# Track all active server processes for cleanup on crash/interrupt
_active_servers: List['ServerProcess'] = []
_active_servers_lock = threading.Lock()


def _cleanup_all_servers():
    """Kill all active server processes. Called via atexit and signal handlers."""
    with _active_servers_lock:
        servers = list(_active_servers)
        _active_servers.clear()
    for server in servers:
        server.cleanup()


atexit.register(_cleanup_all_servers)

# Handle SIGINT/SIGTERM gracefully
def _signal_handler(signum, frame):
    _cleanup_all_servers()
    sys.exit(1)

if sys.platform != 'win32':
    signal.signal(signal.SIGINT, _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)


class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    BOLD = '\033[1m'
    RESET = '\033[0m'

    @staticmethod
    def enabled():
        return sys.stdout.isatty() and (os.name != 'nt' or os.environ.get('TERM'))


def color(text: str, c: str) -> str:
    return f"{c}{text}{Colors.RESET}" if Colors.enabled() else text


class ServerProcess:
    """Manages a compiled server as a subprocess."""

    def __init__(self, exe_path: Path, env: Optional[Dict[str, str]] = None):
        self.exe_path = exe_path
        self.proc: Optional[subprocess.Popen] = None
        self.stdout_lines: List[str] = []
        self.env = env

    def start(self):
        """Start the server subprocess."""
        self.proc = subprocess.Popen(
            [str(self.exe_path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=self.env,
        )
        with _active_servers_lock:
            _active_servers.append(self)

    def wait_ready(self, pattern: str, timeout: float = 10) -> Optional[re.Match]:
        """Read stdout lines until pattern matches or timeout.

        Returns the regex match object (useful for extracting port), or None on timeout.
        """
        if not self.proc or not self.proc.stdout:
            return None

        deadline = time.monotonic() + timeout
        compiled = re.compile(pattern)

        while time.monotonic() < deadline:
            # Check if process died
            if self.proc.poll() is not None:
                # Process exited — drain remaining output
                remaining = self.proc.stdout.read()
                if remaining:
                    for line in remaining.decode('utf-8', errors='replace').splitlines():
                        self.stdout_lines.append(line)
                return None

            # Non-blocking read: try to read a line with a short timeout
            # On Windows, we can't use select on pipes, so use a thread-based approach
            import threading
            line_result = [None]
            def read_line():
                try:
                    line_result[0] = self.proc.stdout.readline()
                except:
                    pass

            t = threading.Thread(target=read_line, daemon=True)
            t.start()
            remaining_time = deadline - time.monotonic()
            t.join(timeout=min(0.5, max(0.1, remaining_time)))

            if line_result[0]:
                line = line_result[0].decode('utf-8', errors='replace').rstrip('\n').rstrip('\r')
                self.stdout_lines.append(line)
                m = compiled.search(line)
                if m:
                    return m

        return None

    def cleanup(self):
        """Terminate the server process. Safe to call multiple times."""
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                try:
                    self.proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    pass
        with _active_servers_lock:
            if self in _active_servers:
                _active_servers.remove(self)


def compile_server(source_path: Path, exe_path: Path,
                   coverage: bool = False) -> Tuple[bool, str]:
    """Compile a server TypeScript file to an executable."""
    env = os.environ.copy()
    env['ICU_DATA'] = str(COMPILER.parent)

    cmd = [str(COMPILER)]
    if coverage:
        cmd.append('--coverage')
    cmd.extend([str(source_path), '-o', str(exe_path)])

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            timeout=120 if coverage else 60,
            encoding='utf-8',
            errors='replace',
            env=env,
        )
        if result.returncode != 0:
            return False, result.stderr or result.stdout or "Unknown compile error"
        return True, ""
    except subprocess.TimeoutExpired:
        return False, "Compilation timed out"
    except Exception as e:
        return False, str(e)


class _NoRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Handler that prevents urllib from following redirects."""
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def http_request(url: str, method: str = 'GET', body: Optional[str] = None,
                 headers: Optional[Dict[str, str]] = None,
                 timeout: float = 5,
                 follow_redirects: bool = True) -> Tuple[int, str, Dict[str, str]]:
    """Make an HTTP request. Returns (status_code, body, headers).

    Returns (-1, error_message, {}) on connection failure.
    """
    req = urllib.request.Request(url, method=method)
    if headers:
        for k, v in headers.items():
            req.add_header(k, v)
    if body:
        req.data = body.encode('utf-8')

    try:
        if follow_redirects:
            resp = urllib.request.urlopen(req, timeout=timeout)
        else:
            opener = urllib.request.build_opener(_NoRedirectHandler)
            resp = opener.open(req, timeout=timeout)
        resp_body = resp.read().decode('utf-8', errors='replace')
        resp_headers = {k.lower(): v for k, v in resp.getheaders()}
        return resp.status, resp_body, resp_headers
    except urllib.error.HTTPError as e:
        try:
            resp_body = e.read().decode('utf-8', errors='replace') if e.fp else ''
        except Exception:
            resp_body = ''
        resp_headers = {k.lower(): v for k, v in e.headers.items()}
        return e.code, resp_body, resp_headers
    except Exception as e:
        return -1, str(e), {}


def run_check(base_url: str, check: Dict[str, Any]) -> Tuple[bool, str]:
    """Run a single HTTP check against the server.

    Returns (passed, detail_message).
    """
    method = check.get('method', 'GET')
    path = check.get('path', '/')
    url = base_url.rstrip('/') + path
    body = check.get('body')
    headers = check.get('headers')

    follow_redirects = check.get('follow_redirects', True)
    status, resp_body, resp_headers = http_request(url, method, body, headers,
                                                    follow_redirects=follow_redirects)

    if status == -1:
        return False, f"{method} {path} -> connection error: {resp_body}"

    details = []
    passed = True

    # Check status code
    expect_status = check.get('expect_status')
    if expect_status is not None and status != expect_status:
        passed = False
        details.append(f"status {status} != expected {expect_status}")

    # Check body equals
    expect_body = check.get('expect_body')
    if expect_body is not None and resp_body != expect_body:
        passed = False
        details.append(f"body mismatch: got {resp_body!r:.100}")

    # Check body contains
    expect_contains = check.get('expect_body_contains')
    if expect_contains is not None and expect_contains not in resp_body:
        passed = False
        details.append(f"body missing {expect_contains!r:.60}")

    # Check JSON body
    expect_json = check.get('expect_json')
    if expect_json is not None:
        try:
            actual_json = json.loads(resp_body)
            if actual_json != expect_json:
                passed = False
                details.append(f"JSON mismatch: got {actual_json!r:.100}")
        except json.JSONDecodeError:
            passed = False
            details.append(f"invalid JSON: {resp_body!r:.100}")

    # Check header
    expect_header = check.get('expect_header')
    if expect_header:
        for hdr_name, hdr_val in expect_header.items():
            actual = resp_headers.get(hdr_name.lower())
            if actual != hdr_val:
                passed = False
                details.append(f"header {hdr_name}: {actual!r} != {hdr_val!r}")

    status_sym = "ok" if passed else "FAIL"
    check_type = ""
    if expect_json is not None:
        check_type = " (json match)"
    elif expect_body is not None:
        check_type = " (body match)"
    elif expect_contains is not None:
        check_type = " (body contains)"

    detail = f"{method} {path} -> {status} {status_sym}{check_type}"
    if details:
        detail += ": " + "; ".join(details)

    return passed, detail


def load_test_config(test_dir: Path) -> Optional[Dict[str, Any]]:
    """Load a test.py config file as a module and extract test config."""
    test_file = test_dir / 'test.py'
    if not test_file.exists():
        return None

    spec = importlib.util.spec_from_file_location(f"server_test_{test_dir.name}", test_file)
    if not spec or not spec.loader:
        return None

    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    config = {}
    for attr in ('SERVER_SOURCE', 'READY_PATTERN', 'READY_TIMEOUT', 'CHECKS',
                 'SERVER_ENV', 'COMPILE_TIMEOUT'):
        if hasattr(mod, attr):
            config[attr] = getattr(mod, attr)

    return config


COVERAGE_DIR = SCRIPT_DIR / '.coverage'


def run_server_test(test_dir: Path, verbose: bool = False,
                    coverage: bool = False) -> Dict[str, Any]:
    """Run a single server test. Returns result dict."""
    test_name = test_dir.name
    result = {
        'name': test_name,
        'passed': False,
        'checks_passed': 0,
        'checks_total': 0,
        'check_details': [],
        'error': None,
        'elapsed': 0.0,
    }
    start = time.time()

    # Load config
    config = load_test_config(test_dir)
    if not config:
        result['error'] = "No test.py found or failed to load"
        result['elapsed'] = time.time() - start
        return result

    # Resolve source path relative to test dir
    source_rel = config.get('SERVER_SOURCE', '')
    source_path = (test_dir / source_rel).resolve()
    if not source_path.exists():
        result['error'] = f"Source not found: {source_path}"
        result['elapsed'] = time.time() - start
        return result

    profraw_path = None
    if coverage:
        COVERAGE_DIR.mkdir(exist_ok=True)
        profraw_path = COVERAGE_DIR / f'{test_name}.profraw'

    # Compile
    exe_path = test_dir / (test_name + get_exe_suffix())
    if verbose:
        print(f"  Compiling {source_path.name}{'  (with --coverage)' if coverage else ''}...")

    ok, err = compile_server(source_path, exe_path, coverage=coverage)
    if not ok:
        result['error'] = f"Compile error: {err}"
        result['elapsed'] = time.time() - start
        return result

    # Start server
    if verbose:
        print(f"  Starting server...")

    server_env = os.environ.copy()
    server_env['ICU_DATA'] = str(COMPILER.parent)
    extra_env = config.get('SERVER_ENV', {})
    server_env.update(extra_env)
    if coverage and profraw_path:
        server_env['LLVM_PROFILE_FILE'] = str(profraw_path)

    server = ServerProcess(exe_path, env=server_env)
    try:
        server.start()

        # Wait for ready
        ready_pattern = config.get('READY_PATTERN', r'listening')
        ready_timeout = config.get('READY_TIMEOUT', 10)

        if verbose:
            print(f"  Waiting for ready (pattern: {ready_pattern!r}, timeout: {ready_timeout}s)...")

        match = server.wait_ready(ready_pattern, timeout=ready_timeout)
        if not match:
            stdout = '\n'.join(server.stdout_lines[-10:]) if server.stdout_lines else '(no output)'
            result['error'] = f"Server did not become ready within {ready_timeout}s.\nOutput:\n{stdout}"
            return result

        # Extract port from match if present
        try:
            port = match.group(1)
        except (IndexError, AttributeError):
            port = '3000'  # Default fallback

        base_url = f"http://localhost:{port}"
        if verbose:
            print(f"  Server ready at {base_url}")

        # Run checks
        checks = config.get('CHECKS', [])
        result['checks_total'] = len(checks)
        all_passed = True

        for check in checks:
            check_passed, detail = run_check(base_url, check)
            result['check_details'].append(detail)
            if check_passed:
                result['checks_passed'] += 1
            else:
                all_passed = False

        result['passed'] = all_passed and len(checks) > 0

    finally:
        server.cleanup()
        # In coverage mode, the background flush thread writes profraw every 2s.
        # The last flush is at most 2s before we killed the server.
        if coverage and verbose and profraw_path:
            if profraw_path.exists():
                sz = profraw_path.stat().st_size
                print(f"  Coverage: {profraw_path.name} ({sz} bytes)")
            else:
                print(f"  Coverage: no profraw generated")
        # Clean up executable
        if exe_path.exists():
            try:
                exe_path.unlink()
            except OSError:
                pass
        # Clean up .lib file (Windows linker artifact)
        lib_path = exe_path.with_suffix('.lib')
        if lib_path.exists():
            try:
                lib_path.unlink()
            except OSError:
                pass

    result['elapsed'] = time.time() - start
    return result


def find_test_dirs() -> List[Path]:
    """Find all test directories under tests/server/."""
    dirs = []
    for entry in sorted(SCRIPT_DIR.iterdir()):
        if entry.is_dir() and not entry.name.startswith('.') and not entry.name.startswith('__'):
            if (entry / 'test.py').exists():
                dirs.append(entry)
    return dirs


class ServerTestRunner:
    """Main runner for server tests."""

    def __init__(self, verbose: bool = False, test_filter: Optional[str] = None,
                 coverage: bool = False, jobs: int = 1):
        self.verbose = verbose
        self.test_filter = test_filter
        self.coverage = coverage
        self.jobs = jobs
        self.results: List[Dict[str, Any]] = []
        self.test_statuses: Dict[str, str] = {}
        self.previous_baseline: Dict[str, str] = {}
        self.regressions: List[str] = []
        self.fixes: List[str] = []
        self._print_lock = threading.Lock()

    def _baseline_path(self) -> Path:
        return SCRIPT_DIR / BASELINE_FILE

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
        for name, status in self.test_statuses.items():
            prev = self.previous_baseline.get(name)
            if prev is None:
                continue
            if prev == 'pass' and status != 'pass':
                self.regressions.append(name)
            elif prev != 'pass' and status == 'pass':
                self.fixes.append(name)

    def run(self) -> int:
        if not COMPILER.exists():
            print(color(f"ERROR: Compiler not found at {COMPILER}", Colors.RED))
            return 1

        self._load_baseline()

        test_dirs = find_test_dirs()
        if self.test_filter:
            test_dirs = [d for d in test_dirs if self.test_filter in d.name]

        if not test_dirs:
            print(color("No server tests found.", Colors.YELLOW))
            return 0

        if self.coverage:
            print(color("\n=== Server Test Suite (with coverage) ===\n", Colors.BOLD))
            # Clean previous profraw files
            if COVERAGE_DIR.exists():
                for f in COVERAGE_DIR.glob('*.profraw'):
                    f.unlink()
        else:
            print(color("\n=== Server Test Suite ===\n", Colors.BOLD))
        print(f"Found {len(test_dirs)} server test(s)\n")

        passed = 0
        failed = 0
        completed = 0

        def apply_result(i_ignored, test_dir: Path, result: Dict[str, Any]):
            nonlocal passed, failed, completed
            with self._print_lock:
                completed += 1
                i = completed
                self.results.append(result)

                status_str = 'pass' if result['passed'] else 'fail'
                if result['error'] and 'Compile error' in result['error']:
                    status_str = 'compile_error'
                self.test_statuses[result['name']] = status_str

                if result['passed']:
                    passed += 1
                    print(f"[{i}/{len(test_dirs)}] {test_dir.name}... "
                          f"{color('PASS', Colors.GREEN)} ({result['elapsed']:.1f}s)")
                else:
                    failed += 1
                    print(f"[{i}/{len(test_dirs)}] {test_dir.name}... "
                          f"{color('FAIL', Colors.RED)} ({result['elapsed']:.1f}s)")

                # Print check details
                if self.verbose or not result['passed']:
                    if result['error']:
                        print(f"  {color(result['error'], Colors.RED)}")
                    for detail in result['check_details']:
                        sym = color('ok', Colors.GREEN) if ' ok' in detail else color('FAIL', Colors.RED)
                        display = detail.replace(' ok', f' {sym}', 1).replace(' FAIL', f' {sym}', 1)
                        print(f"  {display}")

        if self.jobs > 1:
            print(color(f"Using {self.jobs} parallel workers\n", Colors.CYAN))
            with ThreadPoolExecutor(max_workers=self.jobs) as ex:
                futures = {
                    ex.submit(run_server_test, test_dir,
                              verbose=self.verbose, coverage=self.coverage): test_dir
                    for test_dir in test_dirs
                }
                for fut in as_completed(futures):
                    test_dir = futures[fut]
                    apply_result(0, test_dir, fut.result())
        else:
            for i, test_dir in enumerate(test_dirs, 1):
                if not self.verbose:
                    print(f"[{i}/{len(test_dirs)}] {test_dir.name}...", end='', flush=True)
                result = run_server_test(test_dir, verbose=self.verbose,
                                         coverage=self.coverage)
                apply_result(i, test_dir, result)

        # Baseline comparison
        self._compare_baseline()
        self._save_baseline()

        # Summary
        print(color("\n=== Server Test Summary ===\n", Colors.BOLD))
        print(f"Passed: {passed}")
        print(f"Failed: {failed}")

        if self.previous_baseline:
            if self.regressions:
                print(color(f"\nREGRESSIONS ({len(self.regressions)}):", Colors.RED))
                for name in self.regressions:
                    print(color(f"  - {name}", Colors.RED))
            if self.fixes:
                print(color(f"\nFixes ({len(self.fixes)}):", Colors.GREEN))
                for name in self.fixes:
                    print(color(f"  + {name}", Colors.GREEN))
            if not self.regressions and not self.fixes:
                print(color("No regressions.", Colors.GREEN))

        # Coverage report
        if self.coverage:
            self._print_coverage_report()

        return 0 if failed == 0 else 1

    def _print_coverage_report(self):
        """Parse and display aggregate coverage from collected profraw files."""
        if not COVERAGE_DIR.exists():
            return
        profraw_files = sorted(COVERAGE_DIR.glob('*.profraw'))
        if not profraw_files:
            print(color("\nNo profraw files collected.", Colors.YELLOW))
            return

        try:
            from coverage_report import parse_profraw, merge_coverage, print_report
            datasets = []
            for pf in profraw_files:
                try:
                    data = parse_profraw(pf)
                    if data:
                        datasets.append((pf.stem, data))
                except Exception as e:
                    if self.verbose:
                        print(f"  Warning: failed to parse {pf.name}: {e}")
            if datasets:
                merged = merge_coverage(datasets)
                print_report(merged, len(profraw_files), len(datasets))
        except ImportError:
            # Fallback: just report file counts
            print(color(f"\nCoverage: {len(profraw_files)} profraw files collected "
                        f"in {COVERAGE_DIR}", Colors.CYAN))
            for pf in profraw_files:
                sz = pf.stat().st_size
                print(f"  {pf.name}: {sz} bytes")


def main():
    parser = argparse.ArgumentParser(description='ts-aot server test runner')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    parser.add_argument('-t', '--test', type=str, help='Run specific test by name')
    parser.add_argument('--coverage', action='store_true',
                        help='Compile with --coverage and collect profraw files')
    parser.add_argument('-j', '--jobs', type=int, default=1,
                        help='Parallel worker threads (default: 1). Caps at 6 since '
                             'each test spawns a server subprocess.')
    args = parser.parse_args()

    # Cap parallelism to avoid overwhelming the system with server processes
    jobs = min(args.jobs, 6)

    runner = ServerTestRunner(verbose=args.verbose, test_filter=args.test,
                              coverage=args.coverage, jobs=jobs)
    return runner.run()


if __name__ == '__main__':
    sys.exit(main())
