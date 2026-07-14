#!/usr/bin/env python3
"""SoA n-body benchmark harness (docs/design/use-fast.md Phase 3).

Compiles nbody_fast.ts ("use fast" + NativeArray SoA) and nbody_dynamic.ts
(the same algorithm over dynamic number[] arrays), verifies they produce the
identical checksum, then times both and reports the delta. If node is on
PATH, also runs nbody_node.js (the same algorithm, plain JS) as the V8
baseline.

Timings are whole-process wall time (min of --runs), so each contender pays
its own startup (ts-aot: runtime+GC init; node: V8 boot, ~40-80 ms). At this
workload size (~0.2-1.2 s) startup is minor but not zero — see README.

Usage:
    python examples/benchmarks/soa/run.py [--runs N]

Run from the repo root (it locates ts-aot.exe relative to it).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TSAOT = os.path.join(ROOT, "build", "src", "compiler", "Release", "ts-aot.exe")
OUTDIR = os.path.join(ROOT, "tmp")


def compile_one(src, out, extra=None):
    cmd = [TSAOT, os.path.join(HERE, src), "-o", os.path.join(OUTDIR, out)]
    if extra:
        cmd += extra
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"compile failed for {src}:\n{r.stderr}")


def run_capture(cmd):
    return subprocess.run(cmd, capture_output=True, text=True).stdout.strip()


def bench(cmd, runs):
    times = []
    for _ in range(runs):
        t = time.perf_counter()
        subprocess.run(cmd, capture_output=True)
        times.append(time.perf_counter() - t)
    return min(times)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=3)
    args = ap.parse_args()

    if not os.path.exists(TSAOT):
        sys.exit(f"ts-aot.exe not found at {TSAOT} — build first.")

    node = shutil.which("node")

    print("compiling...")
    compile_one("nbody_fast.ts", "nbody_fast.exe")
    compile_one("nbody_dynamic.ts", "nbody_dynamic.exe")

    fast_cmd = [os.path.join(OUTDIR, "nbody_fast.exe")]
    dyn_cmd = [os.path.join(OUTDIR, "nbody_dynamic.exe")]
    node_cmd = [node, os.path.join(HERE, "nbody_node.js")] if node else None

    cf = run_capture(fast_cmd)
    cd = run_capture(dyn_cmd)
    print(f"  fast    checksum: {cf}")
    print(f"  dynamic checksum: {cd}")
    if cf != cd:
        sys.exit("MISMATCH: the two ts-aot paths computed different results!")
    if node_cmd:
        cn = run_capture(node_cmd)
        print(f"  node    checksum: {cn}")
        if cn != cf:
            sys.exit("MISMATCH: node computed a different result!")
    else:
        print("  node not found on PATH — skipping the V8 baseline.")
    print("  checksums match.\n")

    f = bench(fast_cmd, args.runs)
    d = bench(dyn_cmd, args.runs)
    print(f"fast (ts-aot)    : {f * 1000:8.1f} ms")
    print(f"dynamic (ts-aot) : {d * 1000:8.1f} ms   {d / f:5.2f}x slower than fast")
    if node_cmd:
        n = bench(node_cmd, args.runs)
        print(f"node (V8 JIT)    : {n * 1000:8.1f} ms   {n / f:5.2f}x slower than fast"
              if n >= f else
              f"node (V8 JIT)    : {n * 1000:8.1f} ms   {f / n:5.2f}x FASTER than fast")


if __name__ == "__main__":
    main()
