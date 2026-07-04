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
import functools
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
# Child-process priority.  A full sweep is a background batch job — dozens of
# LLVM-bearing ts-aot compiles + test exes. At normal priority it saturates the
# machine and can starve interactive work (notably the Claude session that may
# have launched it in the background), dropping the harness mid-sweep — the
# "we crashed" failures. Spawn every child BELOW-normal so the OS gives
# interactive work CPU preference: the sweep still runs full-tilt when the
# machine is idle but yields the moment something else needs the core.
# TS262_LOW_PRIORITY=0 disables (e.g. dedicated CI hosts).
# ---------------------------------------------------------------------------
if os.environ.get("TS262_LOW_PRIORITY", "1") != "0" and sys.platform == "win32":
    _SPAWN_KW = {"creationflags": subprocess.BELOW_NORMAL_PRIORITY_CLASS}
elif os.environ.get("TS262_LOW_PRIORITY", "1") != "0" and hasattr(os, "nice"):
    # POSIX: renice the child via a preexec hook (best-effort).
    _SPAWN_KW = {"preexec_fn": lambda: os.nice(10)}
else:
    _SPAWN_KW = {}

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
_DYNAMIC_FN_RE = re.compile(r"(?:new\s+|(?<![A-Za-z0-9_.$]))Function\s*\(\s*[\"']")

UNSUPPORTED_FEATURES: Set[str] = {
    # Proposals / stage-3 features we haven't implemented.
    # NOTE: Temporal is now implemented (~96% conformance) and is counted in the
    # default full sweep — it is no longer skipped.
    "ShadowRealm", "Intl.DurationFormat",
    "Intl.Locale-info", "Intl.NumberFormat-v3",
    "decorators",  # TC39 stage-3 decorators differ from TS legacy decorators
    "regexp-duplicate-named-groups",
    "Array.fromAsync",
    "json-parse-with-source",
    # "RegExp.escape",  # Stage B: implemented 2026-07-02
    "explicit-resource-management",
    # "iterator-helpers",  # CONF-P2b: implementing (eager helpers landed)
    # "set-methods",  # Stage B1: implemented 2026-07-02
    # "promise-try",  # Stage B: implemented 2026-07-02
    "regexp-modifiers",
    "source-phase-imports",
    "import-defer",
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
    # 2026-06-09 review: removed stale entries for IMPLEMENTED features —
    # FinalizationRegistry, WeakRef (comments referenced the purged Boehm GC;
    # the custom GC has real weak semantics), resizable-arraybuffer,
    # arraybuffer-transfer (HOST_262_SETUP itself calls buffer.transfer()).
    "cross-realm",
    "caller",
    "Float16Array",

    # eval is supported since EVAL-001 (runtime tree-walking interpreter,
    # indirect/global-scope semantics).
}

# Flags that indicate tests we should skip
UNSUPPORTED_FLAGS: Set[str] = {
    # "module" tests compile since 2026-07-03: the job compiles them with
    # TS_SCRIPT_GOAL removed (module goal - top-level import/export legal)
    # and a "use strict" prologue (module code is strict by default).
    "async",        # Async completion protocol ($DONE) not yet wired
    "CanBlockIsFalse",
    "CanBlockIsTrue",
}

# Every test we actually compile is a Script (module-flagged tests are skipped
# above), so tell ts-aot to use the Script goal — top-level import/export and
# import.meta are then SyntaxErrors, as they must be outside a Module.
os.environ.setdefault("TS_SCRIPT_GOAL", "1")

# Tests we intentionally don't support — SpiderMonkey-staging tests that
# contradict ECMA-262 by asserting SM-specific quirks. Path is relative to
# the test262 BUILD_DIR (matches how paths are stored in results.jsonl).
# Directory-level scope exclusions (2026-07-03 policy review):
#  - staging/: test262's unreviewed incubator. Ours is almost entirely
#    staging/sm — SpiderMonkey's donated engine suite (Firefox jstests
#    mechanically converted), full of shell idioms, Bugzilla-regression
#    quirks, and dynamic-code use. Not part of the reviewed conformance
#    suite; engines report it separately.
#  - intl402/: ECMA-402 (Internationalization API) is a SEPARATE
#    specification from ECMA-262 and outside this project's conformance
#    matrices. (Intl.* feature tags were already skipped; this excludes
#    the remainder that tests base-Intl plumbing.)
#  - language/eval-code and built-ins/eval run since EVAL-001 (runtime
#    tree-walking interpreter; direct-eval caller-scope tests will fail
#    honestly rather than skip).
SKIPPED_PREFIXES = (
    ("staging/", "out of scope: test262 staging incubator (SpiderMonkey suite)"),
    ("intl402/", "out of scope: ECMA-402 Internationalization API"),
)

SKIPPED_PATHS: Set[str] = {
    # SM accepts `await` as IdentifierReference inside class field initializer
    # of an async function (referring to a `var await = 1` declared in
    # surrounding script-mode code). Per ECMA-262 13.1.1, IdentifierReference
    # with [+Await] cannot be `await`. Our parser is spec-correct.
    "staging/sm/fields/await-identifier-script.js",
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

    # Skip *_FIXTURE.js files — these are helper modules meant to be pulled
    # in by other tests via `includes:` metadata, not run standalone. Running
    # them top-level produces spurious crashes.
    tests = [t for t in tests if not t.name.endswith("_FIXTURE.js")]

    if filter_str:
        # Slash-agnostic so a forward-slash filter matches Windows backslash
        # paths (e.g. --filter built-ins/Temporal/PlainTime).
        fs = filter_str.replace("\\", "/")
        tests = [t for t in tests if fs in str(t).replace("\\", "/")]

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
            # The async $DONE completion protocol (doneprintHandle.js +
            # stdout sentinel) is ON by default since 2026-06-09 (H5);
            # set TS262_NO_ASYNC=1 to skip async tests (old behavior).
            if flag == "async" and not os.environ.get("TS262_NO_ASYNC"):
                continue
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


HOST_262_SETUP = r"""// $262: minimal host hook surface for test262 harness files.
// Real spec hosts expose a richer object; we provide the methods that
// detachArrayBuffer.js and IsHTMLDDA-feature tests require.
var $262 = {
    // Detach an ArrayBuffer. ArrayBuffer.prototype.transfer() detaches
    // the receiver per spec, so we just call it and discard the new
    // buffer.
    detachArrayBuffer: function(buffer) {
        if (buffer && typeof buffer.transfer === "function") {
            buffer.transfer();
        }
    },
    // [[IsHTMLDDA]]: a host-defined exotic whose [[Call]] returns
    // undefined. Per Annex B § B.3.7.1: also `IsHTMLDDA == null` and
    // `IsHTMLDDA == undefined` are true; ToBoolean returns false.
    // The runtime sets globalThis.__ts_create_htmldda__ to produce a
    // TsFunction with the is_htmldda flag set; the equality and
    // ToBoolean paths in the runtime check that flag.
    IsHTMLDDA: (typeof globalThis.__ts_create_htmldda__ === "function")
        ? globalThis.__ts_create_htmldda__()
        : function() {},
    // Reference to the global object. test262 occasionally uses
    // $262.global for indirect property access.
    global: globalThis,
    // evalScript: not supported in AOT — return undefined so tests
    // that probe for its existence don't crash.
    evalScript: function(_) {},
};
"""


def build_test_source(test_path: Path, meta: TestMetadata) -> str:
    """Concatenate harness files + test body into a single source string."""
    parts = []

    # Always include sta.js (Test262Error) and assert.js
    if not meta.raw:
        for harness_file in ["sta.js", "assert.js"]:
            p = HARNESS_DIR / harness_file
            if p.exists():
                parts.append(p.read_text(encoding='utf-8'))

        # Inject the $262 host-hook stub before any test/include code
        # runs. detachArrayBuffer.js and IsHTMLDDA tests need this.
        parts.append(HOST_262_SETUP)

        # Async tests ($DONE completion protocol): per INTERPRETING.md the
        # host must define `print` and provide $DONE — doneprintHandle.js
        # defines $DONE in terms of print. The pass criterion is the
        # 'Test262:AsyncTestComplete' stdout sentinel (see _run_test_exe).
        if "async" in meta.flags:
            parts.append("function print(msg) { console.log(msg); }")
            dp = HARNESS_DIR / "doneprintHandle.js"
            if dp.exists():
                parts.append(dp.read_text(encoding='utf-8'))

        # Include any additional harness files
        for inc in meta.includes:
            p = HARNESS_DIR / inc
            if p.exists():
                parts.append(p.read_text(encoding='utf-8'))

    # Add the test body
    parts.append(test_path.read_text(encoding='utf-8'))

    return '\n'.join(parts)


def _extract_failure_reason(stdout: str, stderr: str, returncode: int) -> str:
    """Produce a meaningful reason for a failed test from its output.

    ts-aot's runtime prints uncaught exceptions in a FATAL block where the
    label parts go to stderr and the value parts go to stdout, and the
    thrown Test262Error object has no `.name` field (compiler lowers `new
    Test262Error(msg)` via the generic constructor path, producing
    `{message: msg}`). Neither half contains the literal substring "Error"
    that the original extractor looked for — so ~60% of real assertion
    failures were collapsing into the generic "exit code N" cluster.

    This helper recognizes the ts-aot FATAL/ts_throw format plus standard
    JS error patterns and falls back to the last non-empty line before
    giving up on "exit code N".
    """
    combined = stdout + "\n" + stderr

    # 1. CRASHes and OOM — highest priority so they're not drowned by later
    #    matches. Distinguish real OS-level faults (access violation,
    #    integer divide, stack overflow) from Microsoft C++ EH rethrows
    #    (code 0xe06d7363) which are benign: those are just our runtime's
    #    ts_throw propagating through the VEH handler.
    m = re.search(r"VectoredException.*?code=(0x[0-9a-fA-F]+)", combined)
    if m:
        code = m.group(1).lower()
        # 0xe06d7363 = 'msc' EH magic — a C++ exception, not a true crash.
        # Let the uncaught-exception extractor below handle it.
        if code != "0xe06d7363":
            return f"CRASH: VectoredException {code}"
    if "[TsGC] FATAL: Out of memory" in combined:
        return "CRASH: out of memory"
    if "FATAL:" in combined and "Uncaught" not in combined:
        m = re.search(r"FATAL: (.{0,200})", combined)
        if m:
            return f"FATAL: {m.group(1).strip()}"[:200]

    # 2. Uncaught exceptions from ts_throw. The FATAL block is split across
    #    stdout/stderr. Reconstruct the assertion message by scanning stdout
    #    and stderr line-by-line for the `.message = ` value or the first
    #    non-blank line after `FATAL: Uncaught exception:`.
    if "ts_throw: exceptionStack.size()=0" in combined or "FATAL: Uncaught exception" in combined:
        # Try to pull .message from stderr first (canonical location) then
        # stdout (where our runtime routes the value via printf).
        for stream in (stderr, stdout, combined):
            # [ \t]*= must not span newlines; otherwise we'd jump to the next
            # FATAL block label like `.name =` on a later line.
            m = re.search(r"\.message[ \t]*=[ \t]*(\S.{0,180})", stream)
            if m:
                msg = m.group(1).strip()
                if msg and msg != "undefined":
                    return f"Uncaught Test262Error: {msg}"[:200]
        # Fallback: any non-blank, non-warning stdout line is likely the
        # value printout of the exception.
        for line in stdout.split("\n"):
            line = line.strip()
            if (line and not line.startswith("[") and line != "[object Object]"
                    and line != "undefined"):
                return f"Uncaught: {line}"[:200]
        return "Uncaught exception (no message)"

    # 3. Standard JS error patterns: TypeError, RangeError, etc. These can
    #    legitimately come from test code itself, not only from ts-aot's
    #    FATAL block.
    for pat_name in ("TypeError", "RangeError", "SyntaxError",
                     "ReferenceError", "URIError", "EvalError"):
        m = re.search(rf"\b{pat_name}\b.*", combined)
        if m:
            return m.group(0).strip()[:200]

    # 4. Harness-driven Test262Error with legible prefix (rare — only when
    #    the compiler actually creates a properly-named Error instance).
    m = re.search(r"Test262Error.*", combined)
    if m:
        return m.group(0).strip()[:200]

    # 5. Last-resort: first non-warning, non-empty line of the combined
    #    output gives the operator *something* to grep on.
    for line in combined.split("\n"):
        line = line.strip()
        if line and not line.startswith("["):
            return f"exit code {returncode}: {line[:150]}"[:200]
    return f"exit code {returncode}"


@functools.lru_cache(maxsize=None)
def _shared_runtime_config(compiler_str: str):
    """Opt-in shared-runtime mode (env TS262_SHARED_RUNTIME=1).

    When enabled, each test exe links the runtime as tsruntime_shared.dll
    instead of statically — so the emitted exe is ~115 KB instead of ~3 MB, a
    huge disk win across a full sweep (no per-test 3 MB static binary). We do
    NOT copy the 16 MB DLL next to each exe (--no-copy-runtime); instead the run
    subprocess discovers it via PATH and finds ICU data via ICU_DATA.

    Returns (extra_compile_flags, run_env) or (None, None) when disabled.
    """
    if not os.environ.get("TS262_SHARED_RUNTIME"):
        return None, None
    comp_dir = Path(compiler_str).resolve().parent            # build/src/compiler/Release
    dll_dir = comp_dir.parent.parent / "sharedrt" / "Release" # build/src/sharedrt/Release
    icu_dir = comp_dir                                         # icudt74l.dat sits next to ts-aot.exe
    # Freshness guard: a stale tsruntime_shared.dll (older than ts-aot.exe, e.g.
    # after rebuilding only the compiler) link-bombs EVERY test exe that needs a
    # newly-exported runtime symbol -> the whole sweep records compile_errors,
    # indistinguishable from a real regression cliff. Warn loudly up front.
    dll = dll_dir / "tsruntime_shared.dll"
    try:
        comp_exe = Path(compiler_str).resolve()
        if dll.exists() and comp_exe.exists() and dll.stat().st_mtime < comp_exe.stat().st_mtime:
            sys.stderr.write(
                "\n*** [shared-DLL STALE] tsruntime_shared.dll is OLDER than ts-aot.exe.\n"
                "    A new exported runtime symbol will link-fail across ALL test exes\n"
                "    (every test -> compile_error). Run a FULL `cmake --build` first.\n"
                f"    dll : {dll}\n    exe : {comp_exe}\n\n")
        elif not dll.exists():
            sys.stderr.write(f"\n*** [shared-DLL MISSING] {dll} not found; build sharedrt.\n\n")
    except OSError:
        pass
    run_env = dict(os.environ)
    run_env["PATH"] = str(dll_dir) + os.pathsep + run_env.get("PATH", "")
    run_env.setdefault("ICU_DATA", str(icu_dir))
    return ["--shared-runtime", "--no-copy-runtime"], run_env


def _prepare_test(test_path: Path, compiler: Path, build_dir: Path):
    """Assemble the harness'd source and write the .js. Returns
    (early_result, job): early_result is a TestResult for skip/read/write
    outcomes (job is None); otherwise job is a dict the compile + run phases
    consume. Shared by the per-test and batch paths so classification can't
    diverge between them."""
    start = time.time()

    try:
        source = test_path.read_text(encoding='utf-8')
    except Exception as e:
        return TestResult(test_path, "fail", f"read error: {e}"), None

    meta = parse_frontmatter(source)

    # Path-based skip (SM-specific tests that contradict ECMA-262).
    try:
        rel = str(test_path.relative_to(TEST_DIR)).replace('\\', '/')
        if rel in SKIPPED_PATHS:
            return TestResult(test_path, "skip", "intentionally skipped (SM-specific spec divergence)"), None
        for pref, why in SKIPPED_PREFIXES:
            if rel.startswith(pref):
                return TestResult(test_path, "skip", why), None
    except ValueError:
        pass

    skip_reason = should_skip(meta)
    if skip_reason:
        return TestResult(test_path, "skip", skip_reason), None

    # Dynamic code (eval / Function ctor) runs on the runtime tree-walking
    # interpreter since EVAL-001 — no body-scan gating anymore.

    full_source = build_test_source(test_path, meta)

    # Strict mode; default sloppy (many tests expect sloppy behavior).
    # Module code (flags: [module]) is ALWAYS strict per ES.
    if 'onlyStrict' in meta.flags or 'module' in meta.flags:
        full_source = '"use strict";\n' + full_source

    rel = test_path.relative_to(TEST_DIR)
    out_dir = build_dir / rel.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    tmp_js = out_dir / rel.name
    # Hash-based exe basename to bypass Windows UAC installer-detection
    # heuristics (filenames containing install/update/setup/patch elevate and
    # fail with WinError 740, recorded as spurious crashes). POSIX unaffected.
    import hashlib
    name_hash = hashlib.sha1(rel.as_posix().encode("utf-8")).hexdigest()[:16]
    tmp_exe = tmp_js.parent / ("t" + name_hash + get_exe_suffix())

    try:
        tmp_js.write_text(full_source, encoding='utf-8')
        # Dynamic-import tests load sibling *_FIXTURE.js modules with
        # source-relative specifiers; the compiler resolves those against
        # tmp_js's directory, so mirror the fixtures next to it. Without
        # this every dynamic-import test was a spurious compile_error
        # (fixtures exist only in the original test tree). Copies are
        # idempotent and shared by concurrent siblings — never cleaned up
        # per-job (tiny .js files in the gitignored build dir).
        if "_FIXTURE" in full_source:
            import shutil
            for fx in test_path.parent.glob("*_FIXTURE.js"):
                dst = out_dir / fx.name
                # Also refresh a STALE copy (a size mismatch means an old run
                # staged something else under this name — e.g. the April runs
                # that compiled fixtures as harness'd tests; those poisoned
                # module resolution for every later import of the fixture).
                try:
                    if not dst.exists() or dst.stat().st_size != fx.stat().st_size:
                        shutil.copyfile(fx, dst)
                except OSError:
                    pass  # concurrent copy of the same fixture
    except Exception as e:
        return TestResult(test_path, "fail", f"write error: {e}"), None

    _shared_flags, run_env = _shared_runtime_config(str(compiler))
    return None, {
        "is_module": 'module' in meta.flags,
        "test_path": test_path, "meta": meta, "tmp_js": tmp_js, "tmp_exe": tmp_exe,
        "run_env": run_env, "shared_flags": _shared_flags or [], "start": start,
    }


def _cleanup_job(job):
    """Delete a job's build artifacts. Each exe accumulates (~150 GB/sweep
    statically) and recompiling is unconditional, so keeping them has no
    caching benefit. Set TS262_KEEP_ARTIFACTS=1 to retain for debugging."""
    if os.environ.get("TS262_KEEP_ARTIFACTS"):
        return
    for key in ("tmp_exe", "tmp_js"):
        try:
            job[key].unlink(missing_ok=True)
        except Exception:
            pass


def _classify_compile(job, returncode, stderr):
    """Compile-phase classification. Returns a TestResult when the outcome is
    decided at compile time (negative-parse pass/fail, compile_error), or None
    to proceed to running the exe. returncode is None for a compile timeout."""
    test_path = job["test_path"]
    meta = job["meta"]
    elapsed = (time.time() - job["start"]) * 1000
    if returncode is None:
        return TestResult(test_path, "timeout", "compilation timeout", time_ms=elapsed)
    if returncode != 0:
        # Negative parse tests expect compilation to fail.
        if meta.negative and meta.negative.get('phase') == 'parse':
            return TestResult(test_path, "pass",
                              f"expected parse error: {meta.negative.get('type', '')}",
                              time_ms=elapsed)
        return TestResult(test_path, "compile_error",
                          stderr[:200] if stderr else "compilation failed",
                          time_ms=elapsed, exit_code=returncode, stderr=stderr or "")
    # Compiled OK but a parse error was expected -> fail.
    if meta.negative and meta.negative.get('phase') == 'parse':
        return TestResult(test_path, "fail",
                          "expected parse error but compiled successfully", time_ms=elapsed)
    return None


def _run_test_exe(job, timeout):
    """Run the compiled exe and classify the result; cleans up artifacts."""
    test_path = job["test_path"]
    meta = job["meta"]
    try:
        run = subprocess.run(
            [str(job["tmp_exe"])], capture_output=True, text=True, timeout=timeout,
            encoding='utf-8', errors='replace', env=job["run_env"], **_SPAWN_KW
        )
    except subprocess.TimeoutExpired:
        return TestResult(test_path, "timeout", "execution timeout",
                          time_ms=(time.time() - job["start"]) * 1000)
    except Exception as e:
        return TestResult(test_path, "crash", f"execution exception: {e}",
                          time_ms=(time.time() - job["start"]) * 1000)
    finally:
        _cleanup_job(job)

    elapsed = (time.time() - job["start"]) * 1000
    if meta.negative and meta.negative.get('phase') == 'runtime':
        if run.returncode != 0:
            return TestResult(test_path, "pass",
                              f"expected runtime error: {meta.negative.get('type', '')}",
                              time_ms=elapsed, exit_code=run.returncode)
        return TestResult(test_path, "fail", "expected runtime error but exited 0",
                          time_ms=elapsed, exit_code=0)
    # Async tests pass ONLY via the $DONE sentinel: a clean exit without
    # 'Test262:AsyncTestComplete' means the async chain never completed.
    if "async" in meta.flags:
        out = run.stdout or ""
        if (run.returncode == 0 and "Test262:AsyncTestComplete" in out
                and "Test262:AsyncTestFailure" not in out):
            return TestResult(test_path, "pass", time_ms=elapsed, exit_code=0,
                              stdout=out, stderr=run.stderr or "")
        m = re.search(r"Test262:AsyncTestFailure:(.{0,180})", out)
        if m:
            reason = f"async: {m.group(1).strip()}"
        elif run.returncode == 0:
            reason = "async test exited without calling $DONE"
        else:
            reason = _extract_failure_reason(out, run.stderr or "", run.returncode)
        return TestResult(test_path, "fail", reason, time_ms=elapsed,
                          exit_code=run.returncode, stdout=out, stderr=run.stderr or "")

    if run.returncode == 0:
        return TestResult(test_path, "pass", time_ms=elapsed, exit_code=0,
                          stdout=run.stdout or "", stderr=run.stderr or "")
    reason = _extract_failure_reason(run.stdout or "", run.stderr or "", run.returncode)
    return TestResult(test_path, "fail", reason, time_ms=elapsed,
                      exit_code=run.returncode, stdout=run.stdout or "", stderr=run.stderr or "")


def run_single_test(test_path: Path, compiler: Path, build_dir: Path,
                    timeout: int = 10, verbose: bool = False) -> TestResult:
    """Compile and run a single test262 test (per-test path)."""
    early, job = _prepare_test(test_path, compiler, build_dir)
    if early is not None:
        return early

    # Compile. TSAOT_EXTRA_FLAGS lets a caller inject compiler flags.
    compile_cmd = [str(compiler), str(job["tmp_js"]), "-o", str(job["tmp_exe"])]
    _extra = os.environ.get("TSAOT_EXTRA_FLAGS", "").split()
    if _extra:
        compile_cmd += _extra
    compile_cmd += job["shared_flags"]
    compile_env = None
    if job.get("is_module"):
        # Module goal: top-level import/export must be legal for this test.
        compile_env = dict(os.environ)
        compile_env.pop("TS_SCRIPT_GOAL", None)
    try:
        comp = subprocess.run(compile_cmd, capture_output=True, text=True, timeout=30,
                              encoding='utf-8', errors='replace', env=compile_env,
                              **_SPAWN_KW)
        rc, stderr = comp.returncode, comp.stderr or ""
    except subprocess.TimeoutExpired:
        rc, stderr = None, ""
    except Exception as e:
        _cleanup_job(job)
        return TestResult(test_path, "fail", f"compile exception: {e}",
                          time_ms=(time.time() - job["start"]) * 1000)

    decided = _classify_compile(job, rc, stderr)
    if decided is not None:
        _cleanup_job(job)
        return decided
    return _run_test_exe(job, timeout)


def _compile_batch(jobs, compiler, base_extra_flags):
    """Compile every job in ONE `ts-aot --batch` process. Returns a dict
    {str(tmp_js): rc} where rc is the compiler exit code, or None for a compile
    timeout. Any job missing an RC line (because the batch process crashed or
    timed out before reaching it) is recompiled INDIVIDUALLY so a single bad
    test cannot lose its whole chunk (test262 is adversarial)."""
    if not jobs:
        return {}
    shared_flags = jobs[0]["shared_flags"]
    # Module-goal tests (flags: [module]) need TS_SCRIPT_GOAL removed from the
    # compile env — impossible inside a shared batch process. Compile them
    # per-file with the adjusted env; everything else stays batched. (Without
    # this, a --fast sweep recorded every positive module-code test as
    # compile_error while plain -j runs passed them.)
    rc_by_input = {}
    module_jobs = [j for j in jobs if j.get("is_module")]
    jobs = [j for j in jobs if not j.get("is_module")]
    if module_jobs:
        mod_env = dict(os.environ)
        mod_env.pop("TS_SCRIPT_GOAL", None)
        for j in module_jobs:
            single = [str(compiler), str(j["tmp_js"]), "-o", str(j["tmp_exe"])] \
                     + base_extra_flags + shared_flags
            try:
                sp = subprocess.run(single, capture_output=True, text=True,
                                    timeout=30, encoding='utf-8',
                                    errors='replace', env=mod_env, **_SPAWN_KW)
                rc_by_input[str(j["tmp_js"])] = sp.returncode
            except subprocess.TimeoutExpired:
                rc_by_input[str(j["tmp_js"])] = None
            except Exception:
                rc_by_input[str(j["tmp_js"])] = 1
    if not jobs:
        return rc_by_input
    manifest = jobs[0]["tmp_exe"].parent / ("_batch_" + jobs[0]["tmp_exe"].stem + ".mf")
    try:
        manifest.write_text(
            "".join(f'{j["tmp_js"]}\t{j["tmp_exe"]}\n' for j in jobs), encoding="utf-8")
    except Exception:
        # Couldn't write manifest -> force per-file path for all.
        manifest = None

    if manifest is not None:
        cmd = [str(compiler), "--batch", str(manifest)] + base_extra_flags + shared_flags
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  timeout=max(60, len(jobs) * 10),
                                  encoding='utf-8', errors='replace', **_SPAWN_KW)
            for line in proc.stdout.splitlines():
                parts = line.split("\t")
                if len(parts) >= 3:
                    try:
                        rc_by_input[parts[0]] = int(parts[-1])
                    except ValueError:
                        pass
            # A nonzero rc from the BATCH process is not authoritative:
            # batch mode shares compiler state across entries, and tests
            # that compile sibling modules (dynamic-import fixtures) fail
            # there while compiling clean individually. Drop them so the
            # per-file fallback below retries each one. Cheap: almost all
            # failing TESTS compile clean (runtime fails), so this adds
            # only ~compile_error-count extra spawns per sweep.
            for k in [k for k, rc in rc_by_input.items() if rc not in (0, None)]:
                del rc_by_input[k]
        except subprocess.TimeoutExpired:
            pass  # all unaccounted -> per-file fallback
        except Exception:
            pass
        finally:
            try:
                manifest.unlink(missing_ok=True)
            except Exception:
                pass

    # Per-file fallback for any job the batch didn't report (crash/timeout, or
    # manifest write failure).
    for j in jobs:
        key = str(j["tmp_js"])
        if key in rc_by_input:
            continue
        single = [str(compiler), str(j["tmp_js"]), "-o", str(j["tmp_exe"])] + base_extra_flags + shared_flags
        try:
            sp = subprocess.run(single, capture_output=True, text=True, timeout=30,
                                encoding='utf-8', errors='replace', **_SPAWN_KW)
            rc_by_input[key] = sp.returncode
        except subprocess.TimeoutExpired:
            rc_by_input[key] = None  # compile timeout
        except Exception:
            rc_by_input[key] = 1
    return rc_by_input


def process_chunk(tests, compiler: Path, build_dir: Path, timeout: int) -> List[TestResult]:
    """Batch-compile a chunk of tests in one process, then run + classify each.
    Used by the TS262_BATCH chunked dispatch."""
    results: List[TestResult] = []
    jobs = []
    for tp in tests:
        early, job = _prepare_test(tp, compiler, build_dir)
        if early is not None:
            results.append(early)
        else:
            jobs.append(job)

    base_extra_flags = os.environ.get("TSAOT_EXTRA_FLAGS", "").split()
    rc_by_input = _compile_batch(jobs, compiler, base_extra_flags)

    for job in jobs:
        rc = rc_by_input.get(str(job["tmp_js"]), 1)
        # Batch mode has no per-test compiler stderr; pass "" (classification
        # uses only rc + meta, so pass/fail counts are unaffected).
        decided = _classify_compile(job, rc, "")
        if decided is not None:
            _cleanup_job(job)
            results.append(decided)
        else:
            results.append(_run_test_exe(job, timeout))
    return results


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
        self.auto_baseline = args.auto_baseline
        self.jobs = args.jobs
        self.timeout = args.timeout
        self.time_budget_min = args.time_budget_min
        self.resume = args.resume
        self.fresh = args.fresh
        self.interleave = args.interleave
        self.paths_file = getattr(args, "paths_file", None)
        self.results_jsonl = (Path(args.results_file)
                              if getattr(args, "results_file", None)
                              else RESULTS_JSONL)
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
        if self.fresh and self.results_jsonl.exists():
            self.results_jsonl.unlink()
            print(f"[fresh] removed {self.results_jsonl.name}")

        # Load already-completed results for resume
        completed_map: Dict[str, str] = {}
        if self.resume:
            completed_map = load_completed_set(self.results_jsonl)
            if completed_map:
                print(f"[resume] loaded {len(completed_map)} previously-completed tests")

        # Discover tests
        print("Discovering tests...")
        tests = discover_tests(TEST_DIR, self.category, self.filter_str)
        if not tests:
            print("No tests found.")
            return 1

        # --paths-file: restrict to an explicit test list (stratified sample gate)
        if self.paths_file:
            pf = Path(self.paths_file)
            if not pf.exists():
                print(f"Error: paths file not found: {pf}")
                return 1
            wanted = set()
            for line in pf.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if line and not line.startswith("#"):
                    wanted.add(line.replace("\\", "/"))
            before = len(tests)
            tests = [t for t in tests
                     if str(t.relative_to(TEST_DIR)).replace("\\", "/") in wanted]
            print(f"[paths-file] {len(tests)} of {before} tests matched "
                  f"({len(wanted)} listed)")
            missing = len(wanted) - len(tests)
            if missing:
                print(f"[paths-file] WARNING: {missing} listed paths not found on disk")

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
            # Staleness guard: the regression gate is only meaningful if the
            # baseline reflects the CURRENT compiler. A baseline older than
            # ts-aot.exe silently reports month-old "new passes" and hides
            # regressions that landed into the stale set. Warn loudly.
            try:
                comp_exe = Path(self.compiler).resolve()
                if comp_exe.exists():
                    age_days = (comp_exe.stat().st_mtime - BASELINE_FILE.stat().st_mtime) / 86400.0
                    if age_days > 1.0:
                        sys.stderr.write(
                            f"\n*** [baseline STALE] .test262_baseline.json is {age_days:.0f} day(s) "
                            f"older than ts-aot.exe.\n    The regression gate compares against stale "
                            f"truth: fixed tests show as 'new passes' forever and\n    regressions into "
                            f"the old set are invisible. Re-save with --save-baseline after a clean sweep.\n\n")
            except (OSError, AttributeError):
                pass

        # Open result log (append mode preserves any prior entries for resume)
        self._result_log = ResultLog(self.results_jsonl)

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

        # TS262_BATCH=<chunk_size> enables chunked batch compilation (one
        # ts-aot --batch process per chunk) instead of one spawn per test.
        _batch = os.environ.get("TS262_BATCH", "")
        _chunk = int(_batch) if _batch.isdigit() and int(_batch) > 0 else 0
        try:
            if _chunk and self.jobs > 1:
                results = self._run_chunked(tests, start_time, _chunk)
            elif self.jobs > 1:
                results = self._run_parallel(tests, start_time)
            else:
                results = self._run_sequential(tests, start_time)
        finally:
            self._result_log.close()

        # Add previously-completed results (from resume) to the totals.
        if completed_map:
            # Build the set of this-session rel-paths ONCE (was an O(n^2) scan
            # of `results` per completed entry — crippling on a near-complete
            # resume of ~50k tests).
            ran_this_session = {str(r.path.relative_to(TEST_DIR)) for r in results}
            for rel_path, status in completed_map.items():
                if rel_path in ran_this_session:
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

        # Auto-baseline: refresh when sweep was clean AND non-zero net improvement.
        # `_n_new_passes` and `_n_regressions` are populated in the baseline-compare
        # block above (when baseline exists). Empty baseline => no auto-refresh.
        should_auto_save = (
            self.auto_baseline
            and baseline
            and len(regressions) == 0
            and len(new_passes) > 0
        )

        # Save baseline
        if self.save_baseline or should_auto_save:
            bl = {str(r.path.relative_to(TEST_DIR)): r.status
                  for r in results if r.status != "skip"}
            BASELINE_FILE.write_text(json.dumps(bl, indent=2, sort_keys=True))
            tag = "auto" if should_auto_save and not self.save_baseline else "saved"
            print(f"\nBaseline {tag} to {BASELINE_FILE} "
                  f"({len(bl)} entries; +{len(new_passes)} new passes, "
                  f"{len(regressions)} regressions)")

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

    def _run_chunked(self, tests: List[Path], start_time: float,
                     chunk_size: int) -> List[TestResult]:
        """Chunked dispatch (TS262_BATCH=<chunk>): each pool worker batch-compiles
        a chunk in ONE ts-aot process (amortizing the ~35ms 77MB-binary spawn +
        LLVM init + extension load across the chunk) then runs the chunk's exes.
        A compiler crash is isolated to its chunk by _compile_batch's per-file
        fallback. Results stream to the incremental log as chunks complete."""
        results: List[TestResult] = []
        total = len(tests)
        chunks = [tests[i:i + chunk_size] for i in range(0, total, chunk_size)]
        done = 0

        with ThreadPoolExecutor(max_workers=self.jobs) as executor:
            in_flight: Dict = {}
            chunk_iter = iter(chunks)
            max_in_flight = self.jobs * 2

            def submit_next() -> bool:
                if self._shutdown.is_set() or self._budget_exceeded(start_time):
                    return False
                try:
                    ch = next(chunk_iter)
                except StopIteration:
                    return False
                fut = executor.submit(process_chunk, ch, self.compiler,
                                      self.build_dir, self.timeout)
                in_flight[fut] = ch
                return True

            while len(in_flight) < max_in_flight and submit_next():
                pass

            while in_flight:
                done_set, _ = wait(list(in_flight.keys()), timeout=1.0,
                                   return_when=FIRST_COMPLETED)
                for fut in done_set:
                    ch = in_flight.pop(fut)
                    try:
                        chunk_results = fut.result()
                    except Exception as e:
                        chunk_results = [TestResult(t, "crash", f"chunk error: {e}")
                                         for t in ch]
                    for r in chunk_results:
                        results.append(r)
                        rel = str(r.path.relative_to(TEST_DIR))
                        self._result_log.write(r, rel)
                        done += 1
                    elapsed = time.time() - start_time
                    print(f"  [{done}/{total}] {elapsed:.0f}s...", flush=True)
                    submit_next()
                if (self._shutdown.is_set() or self._budget_exceeded(start_time)) and not in_flight:
                    break

            if self._shutdown.is_set() or self._budget_exceeded(start_time):
                print(f"\n[stop] halted at {done}/{total} (budget/shutdown)")
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
    parser.add_argument("--auto-baseline", action="store_true",
                        help="Auto-save baseline after sweep if zero real regressions "
                             "AND non-zero new passes (encourages refresh after clean "
                             "commits; suppresses stale-baseline false-regression noise).")
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
                        help="Preset: -j 24 --time-budget-min 20 --timeout 8 --resume --interleave")
    parser.add_argument("--consolidate-baseline", action="store_true",
                        help="Build baseline JSON from existing results log and exit")
    parser.add_argument("--paths-file",
                        help="Run only tests whose path (relative to test/) is listed "
                             "in this file, one per line ('/' or '\\' separators; "
                             "'#' comments allowed). Used for the stratified "
                             "regression-sample gate.")
    parser.add_argument("--results-file",
                        help="Override the results JSONL path (default: "
                             ".test262_results.jsonl). Use for sample-gate runs so "
                             "they don't pollute the full-sweep log.")
    parser.add_argument("--one",
                        help="Compile+run ONE test through the EXACT sweep pipeline "
                             "(same harness, $262 stub, strict/async handling, shared "
                             "runtime) and print its verdict + full stderr/stdout. Path "
                             "is relative to test262/test/ (or absolute). Use this to "
                             "diagnose a single test instead of hand-rolling a harness.")
    args = parser.parse_args()

    # --one: single-test verdict via the real pipeline (no baseline/log churn).
    if args.one:
        p = Path(args.one)
        if not p.is_absolute():
            cand = TEST_DIR / args.one
            p = cand if cand.exists() else p
        if not p.exists():
            print(f"Error: test not found: {args.one}\n  (looked under {TEST_DIR})")
            sys.exit(2)
        compiler = get_compiler_path()
        BUILD_DIR.mkdir(parents=True, exist_ok=True)
        res = run_single_test(p, compiler, BUILD_DIR, timeout=args.timeout, verbose=True)
        rel = p.name
        try:
            rel = str(p.resolve().relative_to(TEST_DIR.resolve())).replace('\\', '/')
        except ValueError:
            pass
        print(f"\n=== {rel} ===")
        print(f"STATUS : {res.status}")
        if res.reason:
            print(f"REASON : {res.reason}")
        print(f"EXIT   : {res.exit_code}   ({res.time_ms:.0f} ms)")
        if res.stdout and res.stdout.strip():
            print("--- stdout ---\n" + res.stdout.rstrip())
        if res.stderr and res.stderr.strip():
            print("--- stderr ---\n" + res.stderr.rstrip())
        sys.exit(0 if res.status == "pass" else 1)

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

    # --fast preset: set reasonable defaults for a full-run.
    if args.fast:
        if args.jobs == 1:
            args.jobs = 24  # Default for 12C/24T machines; adjust per host.
        if args.time_budget_min is None:
            args.time_budget_min = 20
        if args.timeout == 10:
            args.timeout = 8
        args.resume = True
        args.interleave = True
        # Bundle the measured fast-sweep recipe so it's one flag, not four env
        # vars: shared runtime (115KB exes, no 302GB blowup), -O0 (test exes need
        # no optimization; halves the LLVM backend + cuts timeouts), and chunked
        # batch compilation (one ts-aot process per 16 tests instead of per test).
        # setdefault lets an explicit env override still win.
        os.environ.setdefault("TS262_SHARED_RUNTIME", "1")
        os.environ.setdefault("TS262_BATCH", "16")

    # Default to -O0 for ALL runs (not just --fast): conformance tests care about
    # correctness, not codegen quality, and O0 compiles ~24x faster than O2. The
    # LLVM O1+ pipeline has a superlinear pass that made large harnesses
    # (testIntl.js, big array/switch literals, the Unicode-identifier tables)
    # blow the 30s compile budget and record spurious "compilation timeouts".
    # An explicit -O in TSAOT_EXTRA_FLAGS still wins; pass --opt to override.
    _extra = os.environ.get("TSAOT_EXTRA_FLAGS", "")
    if "-O" not in _extra:
        os.environ["TSAOT_EXTRA_FLAGS"] = (_extra + " -O0").strip()

    runner = Test262Runner(args)
    sys.exit(runner.run())


if __name__ == "__main__":
    main()
