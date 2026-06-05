#!/usr/bin/env python3
"""Crash-site triage: recompile the crashing test262 tests with -g (debug symbols),
run them, and cluster by the FAULTING FUNCTION from the now-symbolicated runtime
crash stack (Core.cpp's VEH prints `[ts-aot] Crash stack:` with file:line when a PDB
is present). Turns the ~575 black-box crashes into a ranked list of crash SITES --
each site is then a clean fix (a crash both fails its own test AND masks the real
assertion underneath).

Usage:
    python triage_crashes.py            # triage all crashes from the results jsonl
    python triage_crashes.py 150        # cap at first 150 (faster sample)

Reuses run_test262's harness assembly + batch compiler + shared-runtime config.
"""
import os, sys, re, collections, subprocess

# Shared runtime + -O0 like a normal fast sweep; -g added below for symbols.
os.environ.setdefault("TS262_SHARED_RUNTIME", "1")

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_test262 as R           # noqa: E402
import cluster_fails as CF        # noqa: E402


def crash_list():
    seen = CF.load()
    out = []
    for p, r in seen.items():
        if r.get("status") == "fail" and CF.is_crash(r):
            out.append(p.replace("\\", "/"))
    return out


SYS_FRAMES = ("_RTDynamicCast", "_CxxThrowException", "RtlUserThreadStart",
              "BaseThreadInitThunk", "__scrt_common_main_seh", "main", "ts_main",
              "user_main", "RtlRaiseException", "KiUserExceptionDispatch")


def top_site(stderr: str) -> str:
    """First ts-aot frame in the '[ts-aot] Crash stack:' backtrace."""
    if "Crash stack:" not in stderr:
        return "(no symbolicated stack)"
    in_stack = False
    for line in stderr.splitlines():
        if "Crash stack:" in line:
            in_stack = True
            continue
        if not in_stack:
            continue
        m = re.match(r"\s*at\s+(\S+)", line)
        if not m:
            continue
        fn = m.group(1)
        if fn in SYS_FRAMES or fn.startswith("__module_init"):
            continue
        # strip a trailing "(file:line)" already excluded by \S+; return func name
        return fn
    return "(stack had no ts-aot frame)"


def main():
    cap = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else None
    compiler = R.get_compiler_path()
    build_dir = R.BUILD_DIR
    crashes = crash_list()
    if cap:
        crashes = crashes[:cap]
    print(f"triaging {len(crashes)} crashes (recompiling with -g)...")

    # Prepare + batch-compile (with -g) in chunks to bound memory.
    sites = collections.Counter()
    examples = collections.defaultdict(list)
    CHUNK = 200
    done = 0
    for i in range(0, len(crashes), CHUNK):
        chunk = crashes[i:i + CHUNK]
        jobs = []
        for rel in chunk:
            tp = R.TEST_DIR / rel
            early, job = R._prepare_test(tp, compiler, build_dir)
            if job:
                job["_rel"] = rel
                jobs.append(job)
        rc = R._compile_batch(jobs, compiler, ["-g"])
        for job in jobs:
            key = str(job["tmp_js"])
            if rc.get(key) != 0:
                R._cleanup_job(job)
                continue  # didn't compile -> not a runtime crash
            try:
                run = subprocess.run([str(job["tmp_exe"])], capture_output=True,
                                     text=True, timeout=15, encoding="utf-8",
                                     errors="replace", env=job["run_env"])
                site = top_site(run.stderr or "")
            except subprocess.TimeoutExpired:
                site = "(timeout)"
            except Exception as e:
                site = f"(run error: {e})"
            finally:
                R._cleanup_job(job)
            sites[site] += 1
            if len(examples[site]) < 3:
                examples[site].append(job["_rel"].split("test262/test/")[-1]
                                      if "test262/test/" in job["_rel"] else job["_rel"])
            done += 1
        print(f"  ...{done}/{len(crashes)}", flush=True)

    print(f"\n=== crash sites (ranked) ===  [{done} crashes triaged]")
    for site, n in sites.most_common(30):
        print(f"  {n:4d}  {site}")
        for ex in examples[site][:2]:
            print(f"         e.g. {ex}")


if __name__ == "__main__":
    main()
