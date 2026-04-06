#!/usr/bin/env python3
"""test262 ECMAScript Conformance Test Runner for ts-aot.

Discovers test262 test files, parses YAML frontmatter, concatenates harness
files, compiles with ts-aot, runs, and checks results.

Usage:
    python tests/test262/run_test262.py                    # Run all enabled tests
    python tests/test262/run_test262.py --filter addition   # Filter by path substring
    python tests/test262/run_test262.py --category language/expressions
    python tests/test262/run_test262.py --save-baseline     # Save current results as baseline
    python tests/test262/run_test262.py --limit 100         # Run at most N tests
    python tests/test262/run_test262.py -v                  # Verbose output
"""

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed, FIRST_COMPLETED, wait
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from ts_test_platform import get_compiler_path, get_exe_suffix

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).parent
TEST262_DIR = SCRIPT_DIR / "test262"
HARNESS_DIR = TEST262_DIR / "harness"
TEST_DIR = TEST262_DIR / "test"
BASELINE_FILE = SCRIPT_DIR / ".test262_baseline.json"
RESULTS_JSONL = SCRIPT_DIR / ".test262_results.jsonl"
BUILD_DIR = SCRIPT_DIR / "build"

# Features we know we DON'T support — skip tests requiring these
UNSUPPORTED_FEATURES: Set[str] = {
    # Proposals / stage-3 features we haven't implemented
    "Temporal", "ShadowRealm", "Intl.DurationFormat",
    "Intl.Locale-info", "Intl.NumberFormat-v3",
    "decorators",  # TC39 stage-3 decorators differ from TS legacy decorators
    "regexp-duplicate-named-groups",
    "Array.fromAsync",
    "json-parse-with-source",
    "RegExp.escape",
    "explicit-resource-management",
    "iterator-helpers",
    "set-methods",
    "promise-try",
    "regexp-modifiers",
    "source-phase-imports",
    "import-attributes",
    "import-assertions",
    "json-modules",
    "Intl.Segmenter",
    "Intl.DisplayNames-v2",
    "Intl.DateTimeFormat-datetimestyle",
    "Intl.DateTimeFormat-dayPeriod",
    "Intl.DateTimeFormat-fractionalSecondDigits",
    "Intl.DateTimeFormat-formatRange",
    "Intl.ListFormat",
    "Intl.RelativeTimeFormat",
    "Intl.Segmenter",
    "Intl.DisplayNames",

    # Runtime features we don't support
    "SharedArrayBuffer",
    "Atomics",
    "tail-call-optimization",
    "FinalizationRegistry",  # no-op in Boehm GC
    "WeakRef",  # strong ref in Boehm GC — semantics differ
    "cross-realm",
    "caller",
    "resizable-arraybuffer",
    "arraybuffer-transfer",
    "Float16Array",

    # We don't support eval
    "eval",
}

# Flags that indicate tests we should skip
UNSUPPORTED_FLAGS: Set[str] = {
    "module",       # ES module tests need different compilation
    "async",        # Async completion protocol ($DONE) not yet wired
    "CanBlockIsFalse",
    "CanBlockIsTrue",
}


# ---------------------------------------------------------------------------
# Frontmatter parsing
# ---------------------------------------------------------------------------

@dataclass
class TestMetadata:
    """Parsed test262 frontmatter."""
    description: str = ""
    features: List[str] = field(default_factory=list)
    flags: List[str] = field(default_factory=list)
    includes: List[str] = field(default_factory=list)
    negative: Optional[Dict] = None
    es5id: str = ""
    es6id: str = ""
    esid: str = ""
    info: str = ""
    locale: List[str] = field(default_factory=list)
    raw: bool = False


def parse_frontmatter(source: str) -> TestMetadata:
    """Extract YAML frontmatter from a test262 test file."""
    m = re.search(r'/\*---\s*\n(.*?)\n---\*/', source, re.DOTALL)
    if not m:
        return TestMetadata()

    yaml_text = m.group(1)
    meta = TestMetadata()

    # Simple YAML parser (avoids PyYAML dependency)
    current_key = None
    current_list = None

    for line in yaml_text.split('\n'):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue

        # Key-value pair
        kv = re.match(r'^(\w[\w-]*):\s*(.*)', line)
        if kv:
            key, value = kv.group(1), kv.group(2).strip()
            current_key = key

            if key == 'description':
                meta.description = value.strip('"\'> ')
            elif key == 'es5id':
                meta.es5id = value
            elif key == 'es6id':
                meta.es6id = value
            elif key == 'esid':
                meta.esid = value
            elif key == 'info':
                meta.info = value
            elif key == 'features':
                if value.startswith('['):
                    # Inline array: [feat1, feat2]
                    meta.features = [f.strip().strip('"\'')
                                     for f in value.strip('[]').split(',')
                                     if f.strip()]
                else:
                    current_list = meta.features
            elif key == 'flags':
                if value.startswith('['):
                    meta.flags = [f.strip().strip('"\'')
                                  for f in value.strip('[]').split(',')
                                  if f.strip()]
                else:
                    current_list = meta.flags
            elif key == 'includes':
                if value.startswith('['):
                    meta.includes = [f.strip().strip('"\'')
                                     for f in value.strip('[]').split(',')
                                     if f.strip()]
                else:
                    current_list = meta.includes
            elif key == 'locale':
                if value.startswith('['):
                    meta.locale = [f.strip().strip('"\'')
                                   for f in value.strip('[]').split(',')
                                   if f.strip()]
                else:
                    current_list = meta.locale
            elif key == 'negative':
                meta.negative = {}
                current_list = None
            elif key == 'phase' and meta.negative is not None:
                meta.negative['phase'] = value
            elif key == 'type' and meta.negative is not None:
                meta.negative['type'] = value
            else:
                current_list = None
            continue

        # Sub-key under negative
        sub = re.match(r'^\s+(phase|type):\s*(.*)', line)
        if sub and meta.negative is not None:
            meta.negative[sub.group(1)] = sub.group(2).strip()
            continue

        # List item
        li = re.match(r'^\s+-\s+(.*)', line)
        if li and current_list is not None:
            current_list.append(li.group(1).strip().strip('"\''))

    if 'raw' in meta.flags:
        meta.raw = True

    return meta


# ---------------------------------------------------------------------------
# Test discovery and filtering
# ---------------------------------------------------------------------------

def discover_tests(base_dir: Path, category: str = None,
                   filter_str: str = None) -> List[Path]:
    """Find all .js test files under the given directory."""
    if category:
        search_dir = base_dir / category
    else:
        search_dir = base_dir

    if not search_dir.exists():
        print(f"Error: directory not found: {search_dir}")
        return []

    tests = sorted(search_dir.rglob("*.js"))

    if filter_str:
        tests = [t for t in tests if filter_str in str(t)]

    return tests


def should_skip(meta: TestMetadata) -> Optional[str]:
    """Return skip reason if test should be skipped, None otherwise."""
    # Skip tests requiring unsupported features
    for feat in meta.features:
        if feat in UNSUPPORTED_FEATURES:
            return f"unsupported feature: {feat}"

    # Skip tests with unsupported flags
    for flag in meta.flags:
        if flag in UNSUPPORTED_FLAGS:
            return f"unsupported flag: {flag}"

    # Skip locale-specific tests (we don't have full Intl)
    if meta.locale:
        return "locale-specific"

    return None


# ---------------------------------------------------------------------------
# Test execution
# ---------------------------------------------------------------------------

@dataclass
class TestResult:
    path: Path
    status: str  # "pass", "fail", "skip", "compile_error", "timeout", "crash"
    reason: str = ""
    time_ms: float = 0
    exit_code: int = 0
    stdout: str = ""
    stderr: str = ""


class ResultLog:
    """Thread-safe append-only JSONL logger for test results.

    Writes one JSON object per line. Each line is flushed immediately so that
    if the process is killed mid-run, all completed results are preserved.
    """

    def __init__(self, path: Path):
        self.path = path
        self._lock = threading.Lock()
        # Open in append mode — OS handles line atomicity for small writes
        self._file = open(path, 'a', encoding='utf-8', buffering=1)

    def write(self, result: TestResult, rel_path: str) -> None:
        record = {
            "path": rel_path,
            "status": result.status,
            "time_ms": int(result.time_ms),
            "reason": result.reason[:200] if result.reason else "",
        }
        line = json.dumps(record, ensure_ascii=False) + "\n"
        with self._lock:
            self._file.write(line)
            self._file.flush()
            try:
                os.fsync(self._file.fileno())
            except (OSError, AttributeError):
                pass  # not all platforms support fsync on text files

    def close(self) -> None:
        with self._lock:
            try:
                self._file.close()
            except Exception:
                pass


def load_completed_set(jsonl_path: Path) -> Dict[str, str]:
    """Load already-completed test paths and their statuses from the JSONL log.

    Returns a dict mapping relative path -> status. Gracefully handles partial
    final lines from an interrupted run.
    """
    completed: Dict[str, str] = {}
    if not jsonl_path.exists():
        return completed
    try:
        with open(jsonl_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    rec = json.loads(line)
                    p = rec.get("path")
                    s = rec.get("status")
                    if p and s:
                        completed[p] = s
                except json.JSONDecodeError:
                    # Partial line from interrupted write — skip
                    continue
    except Exception as e:
        print(f"Warning: failed to read results log: {e}")
    return completed


def interleave_tests(tests: List[Path], base_dir: Path) -> List[Path]:
    """Reorder tests so early termination gives representative coverage.

    Groups by top-two-level directory (e.g. "language/expressions",
    "built-ins/Array") and round-robins between groups. This way if we stop
    after N tests, we have ~N/G tests from each of G categories instead of
    N tests all from the alphabetically-first category.
    """
    groups: Dict[str, List[Path]] = {}
    for t in tests:
        try:
            rel = t.relative_to(base_dir)
        except ValueError:
            rel = t
        parts = rel.parts
        if len(parts) >= 2:
            key = f"{parts[0]}/{parts[1]}"
        elif len(parts) == 1:
            key = parts[0]
        else:
            key = "_other"
        groups.setdefault(key, []).append(t)

    # Round-robin merge
    merged: List[Path] = []
    while any(groups.values()):
        for key in list(groups.keys()):
            bucket = groups[key]
            if bucket:
                merged.append(bucket.pop(0))
    return merged


def build_test_source(test_path: Path, meta: TestMetadata) -> str:
    """Concatenate harness files + test body into a single source string."""
    parts = []

    # Always include sta.js (Test262Error) and assert.js
    if not meta.raw:
        for harness_file in ["sta.js", "assert.js"]:
            p = HARNESS_DIR / harness_file
            if p.exists():
                parts.append(p.read_text(encoding='utf-8'))

        # Include any additional harness files
        for inc in meta.includes:
            p = HARNESS_DIR / inc
            if p.exists():
                parts.append(p.read_text(encoding='utf-8'))

    # Add the test body
    parts.append(test_path.read_text(encoding='utf-8'))

    return '\n'.join(parts)


def run_single_test(test_path: Path, compiler: Path, build_dir: Path,
                    timeout: int = 10, verbose: bool = False) -> TestResult:
    """Compile and run a single test262 test."""
    start = time.time()

    # Read and parse
    try:
        source = test_path.read_text(encoding='utf-8')
    except Exception as e:
        return TestResult(test_path, "fail", f"read error: {e}")

    meta = parse_frontmatter(source)

    # Check if should skip
    skip_reason = should_skip(meta)
    if skip_reason:
        return TestResult(test_path, "skip", skip_reason)

    # Check for eval usage (we don't support eval)
    if 'eval(' in source and 'eval' not in [f for f in meta.features]:
        # Many tests use eval — skip if it's central to the test
        # Only skip if eval is in the test body, not just in harness
        test_body_start = source.find('---*/')
        if test_body_start != -1:
            test_body = source[test_body_start:]
            if 'eval(' in test_body:
                return TestResult(test_path, "skip", "uses eval")

    # Build concatenated source
    full_source = build_test_source(test_path, meta)

    # Determine strict mode
    strict_only = 'onlyStrict' in meta.flags
    no_strict = 'noStrict' in meta.flags

    if strict_only:
        full_source = '"use strict";\n' + full_source
    # Default: run in sloppy mode (many tests expect sloppy behavior)

    # Write to temp file
    rel = test_path.relative_to(TEST_DIR)
    out_dir = build_dir / rel.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    tmp_js = out_dir / rel.name
    tmp_exe = tmp_js.with_suffix(get_exe_suffix())

    try:
        tmp_js.write_text(full_source, encoding='utf-8')
    except Exception as e:
        return TestResult(test_path, "fail", f"write error: {e}")

    # Compile
    compile_cmd = [str(compiler), str(tmp_js), "-o", str(tmp_exe)]
    try:
        comp = subprocess.run(
            compile_cmd, capture_output=True, text=True, timeout=30,
            encoding='utf-8', errors='replace'
        )
    except subprocess.TimeoutExpired:
        elapsed = (time.time() - start) * 1000
        return TestResult(test_path, "timeout", "compilation timeout",
                          time_ms=elapsed)
    except Exception as e:
        elapsed = (time.time() - start) * 1000
        return TestResult(test_path, "fail", f"compile exception: {e}",
                          time_ms=elapsed)

    if comp.returncode != 0:
        elapsed = (time.time() - start) * 1000
        # For negative tests expecting parse errors, compilation failure = pass
        if meta.negative and meta.negative.get('phase') == 'parse':
            expected_type = meta.negative.get('type', '')
            return TestResult(test_path, "pass",
                              f"expected parse error: {expected_type}",
                              time_ms=elapsed)
        return TestResult(test_path, "compile_error",
                          comp.stderr[:200] if comp.stderr else "compilation failed",
                          time_ms=elapsed, exit_code=comp.returncode,
                          stderr=comp.stderr or "")

    # For negative tests expecting parse errors, if we compiled OK that's a fail
    if meta.negative and meta.negative.get('phase') == 'parse':
        elapsed = (time.time() - start) * 1000
        return TestResult(test_path, "fail",
                          f"expected parse error but compiled successfully",
                          time_ms=elapsed)

    # Run
    try:
        run = subprocess.run(
            [str(tmp_exe)], capture_output=True, text=True, timeout=timeout,
            encoding='utf-8', errors='replace'
        )
    except subprocess.TimeoutExpired:
        elapsed = (time.time() - start) * 1000
        return TestResult(test_path, "timeout", "execution timeout",
                          time_ms=elapsed)
    except Exception as e:
        elapsed = (time.time() - start) * 1000
        return TestResult(test_path, "crash", f"execution exception: {e}",
                          time_ms=elapsed)

    elapsed = (time.time() - start) * 1000

    # Check result
    if meta.negative and meta.negative.get('phase') == 'runtime':
        # Negative runtime test: should exit non-zero
        if run.returncode != 0:
            return TestResult(test_path, "pass",
                              f"expected runtime error: {meta.negative.get('type', '')}",
                              time_ms=elapsed, exit_code=run.returncode)
        else:
            return TestResult(test_path, "fail",
                              "expected runtime error but exited 0",
                              time_ms=elapsed, exit_code=0)

    # Normal test: should exit 0 with no uncaught exceptions
    if run.returncode == 0:
        return TestResult(test_path, "pass", time_ms=elapsed,
                          exit_code=0, stdout=run.stdout or "",
                          stderr=run.stderr or "")
    else:
        reason = ""
        output = (run.stdout or "") + (run.stderr or "")
        # Extract error message
        for line in output.split('\n'):
            if 'Test262Error' in line or 'Error' in line:
                reason = line.strip()[:200]
                break
        if not reason:
            reason = f"exit code {run.returncode}"
        return TestResult(test_path, "fail", reason,
                          time_ms=elapsed, exit_code=run.returncode,
                          stdout=run.stdout or "", stderr=run.stderr or "")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

class Test262Runner:
    def __init__(self, args):
        self.verbose = args.verbose
        self.filter_str = args.filter
        self.category = args.category
        self.limit = args.limit
        self.save_baseline = args.save_baseline
        self.jobs = args.jobs
        self.timeout = args.timeout
        self.time_budget_min = args.time_budget_min
        self.resume = args.resume
        self.fresh = args.fresh
        self.interleave = args.interleave
        self.compiler = get_compiler_path()
        self.build_dir = BUILD_DIR
        # Shutdown flag — set by signal handler
        self._shutdown = threading.Event()
        self._result_log: Optional[ResultLog] = None

    def _on_signal(self, signum, frame):
        if not self._shutdown.is_set():
            print("\n\n[!] Shutdown requested. Waiting for in-flight tests to complete...")
            print("[!] Press Ctrl-C again to force-quit (may lose in-flight results).")
            self._shutdown.set()
        else:
            print("\n[!] Force quit.")
            if self._result_log:
                self._result_log.close()
            sys.exit(130)

    def run(self) -> int:
        if not TEST262_DIR.exists():
            print(f"Error: test262 not found at {TEST262_DIR}")
            print("Clone it: git clone --depth 1 https://github.com/tc39/test262.git tests/test262/test262")
            return 1

        if not self.compiler.exists():
            print(f"Error: compiler not found at {self.compiler}")
            return 1

        # Optionally truncate the results log for a fresh run
        if self.fresh and RESULTS_JSONL.exists():
            RESULTS_JSONL.unlink()
            print(f"[fresh] removed {RESULTS_JSONL.name}")

        # Load already-completed results for resume
        completed_map: Dict[str, str] = {}
        if self.resume:
            completed_map = load_completed_set(RESULTS_JSONL)
            if completed_map:
                print(f"[resume] loaded {len(completed_map)} previously-completed tests")

        # Discover tests
        print("Discovering tests...")
        tests = discover_tests(TEST_DIR, self.category, self.filter_str)
        if not tests:
            print("No tests found.")
            return 1

        # Filter out already-completed tests
        if completed_map:
            before = len(tests)
            tests = [t for t in tests
                     if str(t.relative_to(TEST_DIR)) not in completed_map]
            skipped = before - len(tests)
            if skipped:
                print(f"[resume] skipping {skipped} already-completed tests")

        # Interleave for representative early-termination
        if self.interleave:
            tests = interleave_tests(tests, TEST_DIR)

        if self.limit and self.limit < len(tests):
            tests = tests[:self.limit]

        print(f"Found {len(tests)} test(s) to run")
        if self.time_budget_min:
            print(f"[budget] {self.time_budget_min} min wall-clock budget")
        self.build_dir.mkdir(parents=True, exist_ok=True)

        # Load baseline (for comparison at the end)
        baseline = {}
        if BASELINE_FILE.exists():
            try:
                baseline = json.loads(BASELINE_FILE.read_text())
            except Exception:
                pass

        # Open result log (append mode preserves any prior entries for resume)
        self._result_log = ResultLog(RESULTS_JSONL)

        # Install signal handler for graceful shutdown
        try:
            signal.signal(signal.SIGINT, self._on_signal)
        except ValueError:
            pass  # Not in main thread

        # Run tests
        results: List[TestResult] = []
        counts = {"pass": 0, "fail": 0, "skip": 0,
                  "compile_error": 0, "timeout": 0, "crash": 0}
        start_time = time.time()

        try:
            if self.jobs > 1:
                results = self._run_parallel(tests, start_time)
            else:
                results = self._run_sequential(tests, start_time)
        finally:
            self._result_log.close()

        # Add previously-completed results (from resume) to the totals
        if completed_map:
            # Build synthetic TestResult objects for baseline comparison
            for rel_path, status in completed_map.items():
                if any(str(r.path.relative_to(TEST_DIR)) == rel_path for r in results):
                    continue
                results.append(TestResult(
                    path=TEST_DIR / rel_path,
                    status=status,
                ))

        # Tally
        for r in results:
            counts[r.status] = counts.get(r.status, 0) + 1

        elapsed = time.time() - start_time
        executed = counts["pass"] + counts["fail"] + counts["compile_error"] + counts["timeout"] + counts["crash"]

        # Print summary
        print()
        print("=" * 60)
        print(f"test262 Results ({elapsed:.1f}s)")
        print("-" * 60)
        print(f"  Passed:        {counts['pass']:>5}")
        print(f"  Failed:        {counts['fail']:>5}")
        print(f"  Compile Error: {counts['compile_error']:>5}")
        print(f"  Timeout:       {counts['timeout']:>5}")
        print(f"  Crash:         {counts['crash']:>5}")
        print(f"  Skipped:       {counts['skip']:>5}")
        print(f"  Total Run:     {executed:>5}")
        if executed > 0:
            print(f"  Pass Rate:     {counts['pass']/executed*100:>5.1f}%")
        print("=" * 60)

        # Compare to baseline
        if baseline:
            new_passes = []
            regressions = []
            result_map = {str(r.path.relative_to(TEST_DIR)): r.status for r in results}
            for tpath, old_status in baseline.items():
                new_status = result_map.get(tpath)
                if new_status is None:
                    continue
                if old_status != "pass" and new_status == "pass":
                    new_passes.append(tpath)
                elif old_status == "pass" and new_status != "pass":
                    regressions.append(tpath)

            if new_passes:
                print(f"\nNew passes ({len(new_passes)}):")
                for p in new_passes[:20]:
                    print(f"  + {p}")
                if len(new_passes) > 20:
                    print(f"  ... and {len(new_passes) - 20} more")

            if regressions:
                print(f"\nRegressions ({len(regressions)}):")
                for p in regressions[:20]:
                    print(f"  - {p}")
                if len(regressions) > 20:
                    print(f"  ... and {len(regressions) - 20} more")

            if not regressions:
                print("\nNo regressions.")

        # Save baseline
        if self.save_baseline:
            bl = {str(r.path.relative_to(TEST_DIR)): r.status
                  for r in results if r.status != "skip"}
            BASELINE_FILE.write_text(json.dumps(bl, indent=2, sort_keys=True))
            print(f"\nBaseline saved to {BASELINE_FILE} ({len(bl)} entries)")

        # Print failures in verbose mode
        if self.verbose:
            failures = [r for r in results if r.status in ("fail", "crash")]
            if failures:
                print(f"\nFailure details ({len(failures)}):")
                for r in failures[:50]:
                    rel = r.path.relative_to(TEST_DIR)
                    print(f"  {r.status.upper()}: {rel}")
                    if r.reason:
                        print(f"    {r.reason[:120]}")

        return 1 if counts["fail"] + counts["crash"] > 0 else 0

    def _budget_exceeded(self, start_time: float) -> bool:
        if self.time_budget_min is None:
            return False
        elapsed = time.time() - start_time
        return elapsed >= self.time_budget_min * 60.0

    def _run_sequential(self, tests: List[Path], start_time: float) -> List[TestResult]:
        results = []
        for i, test_path in enumerate(tests):
            if self._shutdown.is_set() or self._budget_exceeded(start_time):
                print(f"\n[stop] halting at {i}/{len(tests)} (budget/shutdown)")
                break
            r = run_single_test(test_path, self.compiler, self.build_dir,
                                self.timeout, self.verbose)
            results.append(r)
            # Write to incremental log immediately
            rel = str(test_path.relative_to(TEST_DIR))
            self._result_log.write(r, rel)
            if self.verbose or r.status in ("fail", "crash"):
                sym = {"pass": ".", "fail": "F", "skip": "S",
                       "compile_error": "C", "timeout": "T", "crash": "X"}
                if self.verbose:
                    print(f"  [{i+1}/{len(tests)}] {sym.get(r.status, '?')} {rel}"
                          f"  ({r.time_ms:.0f}ms)")
            elif (i + 1) % 50 == 0:
                elapsed = time.time() - start_time
                print(f"  [{i+1}/{len(tests)}] {elapsed:.0f}s...", flush=True)
        return results

    def _run_parallel(self, tests: List[Path], start_time: float) -> List[TestResult]:
        results: List[TestResult] = []
        done = 0
        submitted = 0
        total = len(tests)

        with ThreadPoolExecutor(max_workers=self.jobs) as executor:
            # Keep a sliding window of in-flight futures rather than submitting all
            # at once. This lets us stop submitting when the budget expires and
            # avoid wasted work in the queue.
            in_flight: Dict = {}
            test_iter = iter(tests)
            max_in_flight = self.jobs * 4  # small queue depth ahead of workers

            def submit_next() -> bool:
                """Submit the next test. Returns False if no more to submit."""
                nonlocal submitted
                if self._shutdown.is_set() or self._budget_exceeded(start_time):
                    return False
                try:
                    t = next(test_iter)
                except StopIteration:
                    return False
                fut = executor.submit(
                    run_single_test, t, self.compiler, self.build_dir,
                    self.timeout, False  # don't let workers print verbose
                )
                in_flight[fut] = t
                submitted += 1
                return True

            # Prime the queue
            while len(in_flight) < max_in_flight and submit_next():
                pass

            last_progress_time = start_time
            while in_flight:
                # Wait for any future to complete (with short timeout to allow signal checks)
                done_set, _ = wait(list(in_flight.keys()), timeout=1.0,
                                   return_when=FIRST_COMPLETED)

                for fut in done_set:
                    test_path = in_flight.pop(fut)
                    try:
                        r = fut.result()
                    except Exception as e:
                        r = TestResult(test_path, "crash",
                                       f"worker exception: {e}")
                    results.append(r)
                    rel = str(test_path.relative_to(TEST_DIR))
                    self._result_log.write(r, rel)
                    done += 1

                    if self.verbose:
                        sym = {"pass": ".", "fail": "F", "skip": "S",
                               "compile_error": "C", "timeout": "T", "crash": "X"}
                        print(f"  [{done}/{total}] {sym.get(r.status, '?')} {rel}"
                              f"  ({r.time_ms:.0f}ms)", flush=True)

                # Progress update every 5 seconds
                now = time.time()
                if now - last_progress_time >= 5.0:
                    elapsed = now - start_time
                    rate = done / elapsed if elapsed > 0 else 0
                    print(f"  [{done}/{total}] {elapsed:.0f}s  ({rate:.1f} tests/s)",
                          flush=True)
                    last_progress_time = now

                # Keep the queue filled
                while len(in_flight) < max_in_flight:
                    if not submit_next():
                        break

                if (self._shutdown.is_set() or self._budget_exceeded(start_time)) and not in_flight:
                    break

            if self._shutdown.is_set() or self._budget_exceeded(start_time):
                print(f"\n[stop] halted at {done} tests ({submitted} submitted, "
                      f"{total - submitted} not submitted)")

        return results


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="test262 runner for ts-aot")
    parser.add_argument("--filter", "-f", help="Filter tests by path substring")
    parser.add_argument("--category", "-c",
                        help="Test category (e.g., language/expressions/addition)")
    parser.add_argument("--limit", "-n", type=int, default=None,
                        help="Max number of tests to run")
    parser.add_argument("--save-baseline", action="store_true",
                        help="Save results as baseline")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Verbose output")
    parser.add_argument("--jobs", "-j", type=int, default=1,
                        help="Parallel jobs (default: 1)")
    parser.add_argument("--timeout", "-t", type=int, default=10,
                        help="Per-test execution timeout in seconds (default: 10)")
    parser.add_argument("--time-budget-min", type=float, default=None,
                        help="Wall-clock budget in minutes (stops submitting new tests when exceeded)")
    parser.add_argument("--resume", action="store_true",
                        help="Resume from previous run using .test262_results.jsonl")
    parser.add_argument("--fresh", action="store_true",
                        help="Delete previous results log before running")
    parser.add_argument("--interleave", action="store_true",
                        help="Round-robin tests across categories for representative early-termination")
    parser.add_argument("--fast", action="store_true",
                        help="Preset: -j 12 --time-budget-min 20 --timeout 8 --resume --interleave")
    parser.add_argument("--consolidate-baseline", action="store_true",
                        help="Build baseline JSON from existing results log and exit")
    args = parser.parse_args()

    # --consolidate-baseline: build baseline from existing JSONL and exit
    if args.consolidate_baseline:
        completed = load_completed_set(RESULTS_JSONL)
        if not completed:
            print(f"Error: no results in {RESULTS_JSONL}")
            sys.exit(1)
        # Exclude skips from the baseline (same as --save-baseline behavior)
        baseline = {p: s for p, s in completed.items() if s != "skip"}
        BASELINE_FILE.write_text(json.dumps(baseline, indent=2, sort_keys=True))
        print(f"Consolidated baseline: {len(baseline)} entries -> {BASELINE_FILE}")
        sys.exit(0)

    # --fast preset: set reasonable defaults for a full-run
    if args.fast:
        if args.jobs == 1:
            args.jobs = 12
        if args.time_budget_min is None:
            args.time_budget_min = 20
        if args.timeout == 10:
            args.timeout = 8
        args.resume = True
        args.interleave = True

    runner = Test262Runner(args)
    sys.exit(runner.run())


if __name__ == "__main__":
    main()
