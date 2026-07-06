#!/usr/bin/env python3
"""SoA n-body benchmark harness (docs/design/use-fast.md Phase 3).

Compiles nbody_fast.ts ("use fast" + NativeArray SoA) and nbody_dynamic.ts
(the same algorithm over dynamic number[] arrays), verifies they produce the
identical checksum, then times both and reports the delta.

Usage:
    python examples/benchmarks/soa/run.py [--runs N]

Run from the repo root (it locates ts-aot.exe relative to it).
"""
import argparse
import os
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


def run_capture(exe):
    return subprocess.run([os.path.join(OUTDIR, exe)],
                          capture_output=True, text=True).stdout.strip()


def bench(exe, runs):
    times = []
    for _ in range(runs):
        t = time.perf_counter()
        subprocess.run([os.path.join(OUTDIR, exe)], capture_output=True)
        times.append(time.perf_counter() - t)
    return min(times)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=3)
    args = ap.parse_args()

    if not os.path.exists(TSAOT):
        sys.exit(f"ts-aot.exe not found at {TSAOT} — build first.")

    print("compiling...")
    compile_one("nbody_fast.ts", "nbody_fast.exe")
    compile_one("nbody_dynamic.ts", "nbody_dynamic.exe")

    cf = run_capture("nbody_fast.exe")
    cd = run_capture("nbody_dynamic.exe")
    print(f"  fast    checksum: {cf}")
    print(f"  dynamic checksum: {cd}")
    if cf != cd:
        sys.exit("MISMATCH: the two paths computed different results!")
    print("  checksums match.\n")

    f = bench("nbody_fast.exe", args.runs)
    d = bench("nbody_dynamic.exe", args.runs)
    print(f"fast    : {f * 1000:8.1f} ms")
    print(f"dynamic : {d * 1000:8.1f} ms")
    print(f"ratio   : {d / f:6.2f}x  (>1 = fast wins)")


if __name__ == "__main__":
    main()
