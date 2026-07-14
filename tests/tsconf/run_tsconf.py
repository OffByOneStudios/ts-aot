#!/usr/bin/env python3
"""TSCONF-001 runner — Phase 1 (acceptance) + Phase 2 (runtime oracle).

Axis 1 — acceptance: every classify()=run test compiles with `ts-aot -c`.
  status: pass | compile_error | crash | timeout        (no .errors.txt)
          neg_accept | neg_reject | neg_crash | neg_timeout  (.errors.txt)
  Headline % = pass / (pass + compile_error + crash + timeout).
  Permissive policy (user default): neg_accept is FINE; neg_crash gates at 0.

Axis 2 — runtime oracle: for RUNTIME_DIRS tests that are non-negative and
compile, build+run with ts-aot (--shared-runtime) and diff stdout/exit
against node executing tsc-6.0.3-emitted JS (oracle outputs cached in
oracle_cache.json keyed by content hash).
  status: runtime_match | runtime_diff | runtime_crash | runtime_timeout
          | oracle_error (tsc/node itself failed -> counted skip)

Usage:
  python tests/tsconf/run_tsconf.py accept  [-j N] [--limit N] [--dir SUB]
  python tests/tsconf/run_tsconf.py oracle  [-j N] [--limit N]
  python tests/tsconf/run_tsconf.py report
Baselines: .tsconf_baseline.json (acceptance), .tsconf_rt_baseline.json.
Results:   .tsconf_results.jsonl (both axes, merged per run).
"""
import argparse
import collections
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CONF = os.path.join(HERE, "upstream", "TypeScript", "tests", "cases", "conformance")
BASELINE_REF = os.path.join(HERE, "upstream", "TypeScript", "tests", "baselines", "reference")
TSAOT = os.path.join(ROOT, "build", "src", "compiler", "Release", "ts-aot.exe")
SHARED_RT_DIR = os.path.join(ROOT, "build", "src", "sharedrt", "Release")
ICU_DIR = os.path.join(ROOT, "build", "src", "compiler", "Release")
TSC_JS = os.path.join(HERE, "oracle", "node_modules", "typescript", "lib", "tsc.js")
RESULTS = os.path.join(HERE, ".tsconf_results.jsonl")
ACC_BASELINE = os.path.join(HERE, ".tsconf_baseline.json")
RT_BASELINE = os.path.join(HERE, ".tsconf_rt_baseline.json")
ORACLE_CACHE = os.path.join(HERE, "oracle_cache.json")

sys.path.insert(0, HERE)
from parse_meta import parse_test          # noqa: E402
from skip_policy import classify           # noqa: E402

# Ticket-scoped runtime-feature directories (observable emit semantics).
RUNTIME_DIRS = {"enums", "constEnums", "decorators", "classes",
                "asyncGenerators", "generators"}

CRASH_EXITS = {0xC0000005, 0xC0000409, 0xC0000374, 0x80000003}


def is_crash_exit(rc):
    return rc is not None and (rc & 0xFFFFFFFF) in CRASH_EXITS


_NEG_BASES = None

def is_negative(path):
    """A test is negative iff ANY variant baseline has an .errors.txt --
    comma-valued @target/@module tests emit VARIANT-SUFFIXED baselines
    (`name(target=esnext).errors.txt`); the unsuffixed check alone
    misclassified 2,789 of them as positive."""
    global _NEG_BASES
    if _NEG_BASES is None:
        s = set()
        for fn in os.listdir(BASELINE_REF):
            if fn.endswith(".errors.txt"):
                stem = fn[:-len(".errors.txt")]
                s.add(stem.split("(")[0])
        _NEG_BASES = s
    base = os.path.splitext(os.path.basename(path))[0]
    return base in _NEG_BASES


def discover():
    out = []
    for dirpath, _dirs, files in os.walk(CONF):
        for fn in sorted(files):
            if fn.endswith((".ts", ".tsx")):
                out.append(os.path.join(dirpath, fn))
    return sorted(out)


def rel(p):
    return os.path.relpath(p, CONF).replace("\\", "/")


# --- axis 1 -------------------------------------------------------------------

def run_accept_one(path, objdir):
    tc = parse_test(path)
    v, reason = classify(tc, CONF)
    if v == "skip":
        return {"path": rel(path), "axis": "accept", "status": "skip", "reason": reason}
    neg = is_negative(path)
    obj = os.path.join(objdir, hashlib.sha1(path.encode()).hexdigest()[:16] + ".obj")
    t0 = time.time()
    try:
        r = subprocess.run([TSAOT, path, "-c", "-o", obj],
                           capture_output=True, text=True, timeout=60,
                           encoding="utf-8", errors="replace")
        rc = r.returncode
        err = (r.stderr or "") + (r.stdout or "")
    except subprocess.TimeoutExpired:
        rc, err = None, "timeout"
    ms = int((time.time() - t0) * 1000)
    try:
        if os.path.exists(obj):
            os.remove(obj)
    except OSError:
        pass
    if rc is None:
        st = "neg_timeout" if neg else "timeout"
    elif is_crash_exit(rc):
        st = "neg_crash" if neg else "crash"
    elif rc == 0:
        st = "neg_accept" if neg else "pass"
    else:
        st = "neg_reject" if neg else "compile_error"
    reason_txt = ""
    if st in ("compile_error", "crash"):
        for line in err.splitlines():
            if "error" in line.lower() or "Error" in line:
                reason_txt = line.strip()[:160]
                break
        if not reason_txt:
            reason_txt = err.strip().splitlines()[-1][:160] if err.strip() else f"exit={rc}"
    return {"path": rel(path), "axis": "accept", "status": st,
            "reason": reason_txt, "time_ms": ms}


# --- axis 2 -------------------------------------------------------------------

def load_oracle_cache():
    if os.path.exists(ORACLE_CACHE):
        with open(ORACLE_CACHE, encoding="utf-8") as f:
            return json.load(f)
    return {}


def oracle_key(path, text):
    return hashlib.sha1(("v6.0.3|" + text).encode("utf-8", "replace")).hexdigest()


def tsc_options(tc):
    opts = ["--target", "es2020", "--module", "commonjs", "--skipLibCheck", "--noCheck"]
    if "experimentaldecorators" in tc.options:
        opts += ["--experimentalDecorators"]
    return opts


def load_acc_baseline():
    if os.path.exists(ACC_BASELINE):
        with open(ACC_BASELINE, encoding="utf-8") as f:
            return json.load(f)
    return {}


def run_oracle_one(path, cache, cache_lock, workdir, acc_status):
    # Axis 2 runs only tests that PASSED axis 1 (compile-only). Without this
    # filter, acceptance failures leaked in as runtime_diff and poisoned the
    # match denominator (first sweep: 112 of 168 diffs were compile fails).
    if acc_status.get(rel(path)) != "pass":
        return None
    tc = parse_test(path)
    v, _reason = classify(tc, CONF)
    if v == "skip" or is_negative(path):
        return None
    text = open(path, encoding="utf-8", errors="replace").read()
    key = oracle_key(path, text)
    tid = hashlib.sha1(path.encode()).hexdigest()[:16]
    tdir = os.path.join(workdir, tid)
    os.makedirs(tdir, exist_ok=True)

    # 1. oracle side (cached): tsc emit + node run
    with cache_lock:
        cached = cache.get(key)
    if cached is None:
        try:
            r = subprocess.run(["node", TSC_JS, *tsc_options(tc), "--outDir", tdir, path],
                               capture_output=True, text=True, timeout=60,
                               encoding="utf-8", errors="replace")
            emitted = [os.path.join(tdir, f) for f in os.listdir(tdir) if f.endswith(".js")]
            if not emitted:
                cached = {"oracle_error": f"tsc emitted nothing (exit {r.returncode})"}
            else:
                rn = subprocess.run(["node", emitted[0]], capture_output=True, text=True,
                                    timeout=15, encoding="utf-8", errors="replace")
                cached = {"stdout": rn.stdout, "exit": rn.returncode,
                          "stderr_head": (rn.stderr or "")[:200]}
                if rn.returncode != 0 and "ReferenceError" in (rn.stderr or ""):
                    # Ambient `declare` names have no runtime existence; the
                    # emitted JS cannot run under node. Not scoreable.
                    cached = {"oracle_error": "oracle-unrunnable-ambient: " +
                              (rn.stderr or "").splitlines()[0][:120] if rn.stderr else
                              "oracle-unrunnable-ambient"}
        except subprocess.TimeoutExpired:
            cached = {"oracle_error": "oracle timeout"}
        except OSError as e:
            cached = {"oracle_error": str(e)[:120]}
        with cache_lock:
            cache[key] = cached
    if "oracle_error" in cached:
        return {"path": rel(path), "axis": "runtime", "status": "oracle_error",
                "reason": cached["oracle_error"]}

    # 2. ts-aot side: compile+link (shared runtime) + run
    exe = os.path.join(tdir, "t.exe")
    try:
        r = subprocess.run([TSAOT, path, "--shared-runtime", "-o", exe],
                           capture_output=True, text=True, timeout=90,
                           encoding="utf-8", errors="replace")
        if r.returncode != 0 or not os.path.exists(exe):
            return {"path": rel(path), "axis": "runtime", "status": "runtime_diff",
                    "reason": "full compile+link failed though -c passed"}
        env = dict(os.environ)
        env["ICU_DATA"] = ICU_DIR
        env["PATH"] = SHARED_RT_DIR + os.pathsep + env.get("PATH", "")
        rn = subprocess.run([exe], capture_output=True, text=True, timeout=15,
                            encoding="utf-8", errors="replace", env=env)
    except subprocess.TimeoutExpired:
        return {"path": rel(path), "axis": "runtime", "status": "runtime_timeout", "reason": ""}
    if is_crash_exit(rn.returncode):
        return {"path": rel(path), "axis": "runtime", "status": "runtime_crash",
                "reason": f"exit={rn.returncode & 0xFFFFFFFF:#x}"}
    ours = (rn.stdout or "").replace("\r\n", "\n").strip()
    theirs = (cached["stdout"] or "").replace("\r\n", "\n").strip()
    ok = ours == theirs and (rn.returncode == 0) == (cached["exit"] == 0)
    if ok:
        return {"path": rel(path), "axis": "runtime", "status": "runtime_match", "reason": ""}
    why = "exit mismatch" if ours == theirs else \
          f"stdout differs (ours {len(ours)}ch vs oracle {len(theirs)}ch)"
    return {"path": rel(path), "axis": "runtime", "status": "runtime_diff", "reason": why[:160]}


# --- drivers ------------------------------------------------------------------

def compare_and_save_baseline(results, baseline_path, axis):
    cur = {r["path"]: r["status"] for r in results if r["axis"] == axis and r["status"] != "skip"}
    old = {}
    if os.path.exists(baseline_path):
        with open(baseline_path, encoding="utf-8") as f:
            old = json.load(f)
    GOOD = {"pass", "neg_accept", "neg_reject", "runtime_match"}
    new_pass = [p for p, s in cur.items() if s in GOOD and old.get(p) not in (None, s) and old.get(p) not in GOOD]
    regress = [p for p, s in cur.items() if s not in GOOD and old.get(p) in GOOD]
    # Save only CLEAN sweeps (test262 --auto-baseline discipline). Saving on
    # regressions absorbed the regression list into the baseline and made
    # back-to-back sweeps lie -- bit three separate sessions.
    if not regress or os.environ.get("TSCONF_FORCE_BASELINE"):
        with open(baseline_path, "w", encoding="utf-8") as f:
            json.dump(cur, f, indent=0, sort_keys=True)
    else:
        print("  (baseline NOT saved: regressions present; "
              "set TSCONF_FORCE_BASELINE=1 to override)")
    return new_pass, regress


TSC_DIFF_CACHE = os.path.join(HERE, "tsc_diff_cache.json")


def tsc_syntax_verdict(path, text, cache, cache_lock):
    """tsc-as-reference-parser: 'ok' | 'syntax_error' | 'tsc_fail'.
    --noCheck suppresses semantic diagnostics; what remains (TS1xxx) is
    grammar. Cached by content hash."""
    key = hashlib.sha1(("diff-v6.0.3|" + text).encode("utf-8", "replace")).hexdigest()
    with cache_lock:
        if key in cache:
            return cache[key]
    try:
        r = subprocess.run(["node", TSC_JS, "--noEmit", "--noCheck", "--skipLibCheck",
                            "--target", "esnext", path],
                           capture_output=True, text=True, timeout=60,
                           encoding="utf-8", errors="replace")
        out = (r.stdout or "") + (r.stderr or "")
        import re as _re
        syn = _re.findall(r"error TS1\d{3}", out)
        v = "syntax_error" if syn else ("ok" if r.returncode in (0, 2) else "tsc_fail")
        # returncode 2 = semantic diagnostics only (suppressed classes vary)
        if r.returncode not in (0, 2) and not syn:
            v = "tsc_fail"
    except Exception:
        v = "tsc_fail"
    with cache_lock:
        cache[key] = v
    return v


def run_differential(tests, jobs):
    """Compare our parse/compile verdict against tsc's SYNTAX verdict.
    FALSE REJECTS (tsc ok, we reject at PARSE) are our parser bugs;
    they are the drift signal the structural recognizer is gated on."""
    import threading
    acc = load_acc_baseline()
    cache = {}
    if os.path.exists(TSC_DIFF_CACHE):
        with open(TSC_DIFF_CACHE, encoding="utf-8") as f:
            cache = json.load(f)
    lock = threading.Lock()

    ours_parse_reject = set()
    with open(RESULTS, encoding="utf-8") as f:
        last = {}
        for line in f:
            r = json.loads(line)
            if r["axis"] == "accept":
                last[r["path"]] = r
        for p, r in last.items():
            reason = r.get("reason", "")
            if r["status"] in ("compile_error", "neg_reject") and (
                    "SyntaxError" in reason or "Expected" in reason or
                    "Unexpected token" in reason):
                ours_parse_reject.add(p)

    rows = []
    def one(path):
        rp = rel(path)
        st = acc.get(rp)
        if st is None:
            return None
        text = open(path, encoding="utf-8", errors="replace").read()
        tv = tsc_syntax_verdict(path, text, cache, lock)
        return (rp, st, tv, rp in ours_parse_reject)

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        done = 0
        for res in ex.map(one, tests):
            if res:
                rows.append(res)
            done += 1
            if done % 400 == 0:
                print(f"  [{done}/{len(tests)}]", flush=True)
    with open(TSC_DIFF_CACHE, "w", encoding="utf-8") as f:
        json.dump(cache, f, indent=0, sort_keys=True)

    GOOD = {"pass", "neg_accept"}
    false_rejects = [(p, st) for p, st, tv, isparse in rows
                     if tv == "ok" and st in ("compile_error", "crash") and isparse]
    other_rejects = [(p, st) for p, st, tv, isparse in rows
                     if tv == "ok" and st in ("compile_error", "crash") and not isparse]
    false_accepts = [(p, st) for p, st, tv, _ in rows
                     if tv == "syntax_error" and st in GOOD]
    agree_ok = sum(1 for _, st, tv, _ in rows if tv == "ok" and st in GOOD)
    print(f"\n== tsc parse-differential ({len(rows)} scored) ==")
    print(f"  agree (both accept):                {agree_ok}")
    print(f"  FALSE PARSE REJECTS (tsc ok, we syntax-error): {len(false_rejects)}")
    print(f"  analyzer rejects (tsc ok, we error post-parse): {len(other_rejects)}")
    print(f"  FALSE ACCEPTS (tsc syntax-error, we accept):    {len(false_accepts)}")
    for p, st in false_rejects[:15]:
        print(f"    FR {p}")
    for p, st in false_accepts[:10]:
        print(f"    FA {p}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["accept", "oracle", "report", "diff"])
    ap.add_argument("-j", type=int, default=16)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--dir", default="")
    args = ap.parse_args()

    tests = discover()
    if args.dir:
        tests = [t for t in tests if rel(t).startswith(args.dir)]
    if args.mode == "oracle":
        tests = [t for t in tests
                 if rel(t).split("/")[0] in RUNTIME_DIRS]
    if args.limit:
        tests = tests[:args.limit]

    if args.mode == "diff":
        runnable = [t for t in tests]
        return run_differential(runnable, args.j)

    if args.mode == "report":
        counts = collections.Counter()
        with open(RESULTS, encoding="utf-8") as f:
            for line in f:
                r = json.loads(line)
                counts[(r["axis"], r["status"])] += 1
        for (axis, st), n in sorted(counts.items()):
            print(f"{axis:8} {st:16} {n}")
        return 0

    results = []
    t0 = time.time()
    workdir = tempfile.mkdtemp(prefix="tsconf_")
    if args.mode == "accept":
        with ThreadPoolExecutor(max_workers=args.j) as ex:
            futs = {ex.submit(run_accept_one, t, workdir): t for t in tests}
            done = 0
            for fut in as_completed(futs):
                results.append(fut.result())
                done += 1
                if done % 400 == 0:
                    print(f"  [{done}/{len(tests)}] {time.time()-t0:.0f}s", flush=True)
        new_pass, regress = compare_and_save_baseline(results, ACC_BASELINE, "accept")
    else:
        import threading
        cache = load_oracle_cache()
        lock = threading.Lock()
        acc_status = load_acc_baseline()
        with ThreadPoolExecutor(max_workers=args.j) as ex:
            futs = {ex.submit(run_oracle_one, t, cache, lock, workdir, acc_status): t for t in tests}
            done = 0
            for fut in as_completed(futs):
                r = fut.result()
                if r:
                    results.append(r)
                done += 1
                if done % 100 == 0:
                    print(f"  [{done}/{len(tests)}] {time.time()-t0:.0f}s", flush=True)
        with open(ORACLE_CACHE, "w", encoding="utf-8") as f:
            json.dump(cache, f, indent=0, sort_keys=True)
        new_pass, regress = compare_and_save_baseline(results, RT_BASELINE, "runtime")

    with open(RESULTS, "a", encoding="utf-8") as f:
        for r in results:
            f.write(json.dumps(r) + "\n")

    counts = collections.Counter(r["status"] for r in results)
    print(f"\n== {args.mode} sweep ({time.time()-t0:.0f}s, {len(results)} results) ==")
    for st, n in counts.most_common():
        print(f"  {st:16} {n}")
    if args.mode == "accept":
        denom = sum(counts[s] for s in ("pass", "compile_error", "crash", "timeout"))
        if denom:
            print(f"  ACCEPTANCE: {counts['pass']}/{denom} = {100.0*counts['pass']/denom:.1f}%")
        print(f"  neg_crash (must be 0): {counts['neg_crash']}")
    else:
        denom = sum(counts[s] for s in
                    ("runtime_match", "runtime_diff", "runtime_crash", "runtime_timeout"))
        if denom:
            print(f"  RUNTIME MATCH: {counts['runtime_match']}/{denom} = "
                  f"{100.0*counts['runtime_match']/denom:.1f}%")
    print(f"  new passes: {len(new_pass)}, regressions: {len(regress)}")
    for p in regress[:20]:
        print(f"    - {p}")
    return 1 if regress else 0


if __name__ == "__main__":
    sys.exit(main())
