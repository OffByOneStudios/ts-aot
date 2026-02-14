#!/usr/bin/env python3
"""
Fair GC Comparison: ts-aot vs Node.js

Runs identical GC workloads on both runtimes and compares:
- Wall-clock time per workload
- Peak heap usage
- GC pause count and total time (Node.js only - via PerformanceObserver)
- Memory efficiency (heap after GC / peak heap)

Usage:
    python scripts/gc_compare.py [OPTIONS]

Options:
    --mode fair         Constrain V8 heap to 256MB (default)
    --mode default      Use V8 defaults (~1.4GB heap)
    --mode no-concurrent  Disable V8 concurrent/incremental marking
    --iterations N      Run N times and report min/avg/max (default: 3)
    --verbose           Show raw output from both runtimes
    --aot-only          Only run ts-aot
    --node-only         Only run Node.js
    --gc-verbose        Set TS_GC_VERBOSE=1 for ts-aot runs
"""

import subprocess
import sys
import os
import re
import argparse
import statistics
from pathlib import Path

# ============================================================================
# Configuration
# ============================================================================

PROJECT_ROOT = Path(__file__).parent.parent
COMPILER = PROJECT_ROOT / "build" / "src" / "compiler" / "Release" / "ts-aot.exe"
BENCHMARK_TS = PROJECT_ROOT / "examples" / "benchmarks" / "memory" / "gc_compare.ts"
NODE_RUNNER = PROJECT_ROOT / "scripts" / "gc_compare_node_runner.js"
AOT_EXE = PROJECT_ROOT / "tmp" / "gc_compare.exe"

V8_MODES = {
    "fair": [
        "--expose-gc",
        "--max-old-space-size=256",
        "--max-semi-space-size=4",
    ],
    "default": [
        "--expose-gc",
    ],
    "no-concurrent": [
        "--expose-gc",
        "--max-old-space-size=256",
        "--max-semi-space-size=4",
        "--no-incremental-marking",
        "--no-concurrent-marking",
    ],
}

# ============================================================================
# Output parsing
# ============================================================================

def parse_output(output):
    """Parse structured output from either runtime."""
    data = {
        "runtime": None,
        "heap_initial": 0,
        "rss_initial": 0,
        "heap_final": 0,
        "rss_final": 0,
        "v8_heap_limit": 0,
        "workloads": {},
        "gc_stats": {},
        "gc_summary": None,
    }

    for line in output.strip().splitlines():
        line = line.strip()

        if line.startswith("RUNTIME:"):
            data["runtime"] = line.split(":", 1)[1]

        elif line.startswith("HEAP_INITIAL:"):
            data["heap_initial"] = int(line.split(":", 1)[1])

        elif line.startswith("RSS_INITIAL:"):
            data["rss_initial"] = int(line.split(":", 1)[1])

        elif line.startswith("HEAP_FINAL:"):
            data["heap_final"] = int(line.split(":", 1)[1])

        elif line.startswith("RSS_FINAL:"):
            data["rss_final"] = int(line.split(":", 1)[1])

        elif line.startswith("V8_HEAP_SIZE_LIMIT:"):
            data["v8_heap_limit"] = int(line.split(":", 1)[1])

        elif line.startswith("RESULT:"):
            # RESULT:name|wall=X|heapBefore=X|heapAfter=X|peakHeap=X|rss=X|items=X|checksum=X
            parts = line[7:].split("|")
            name = parts[0]
            fields = {}
            for part in parts[1:]:
                k, v = part.split("=", 1)
                try:
                    # Handle scientific notation (e.g., 5e+06, 1.25e+12)
                    fields[k] = float(v)
                except ValueError:
                    fields[k] = 0
            data["workloads"][name] = fields

        elif line.startswith("GC_STATS:"):
            # GC_STATS:name|count=X|totalMs=X|maxPauseMs=X|gcPct=X
            parts = line[9:].split("|")
            name = parts[0]
            fields = {}
            for part in parts[1:]:
                k, v = part.split("=", 1)
                fields[k] = float(v)
            data["gc_stats"][name] = fields

        elif line.startswith("GC_SUMMARY"):
            parts = line.split("|")
            fields = {}
            for part in parts[1:]:
                k, v = part.split("=", 1)
                fields[k] = float(v)
            data["gc_summary"] = fields

    return data


# ============================================================================
# Build ts-aot benchmark
# ============================================================================

def build_aot(verbose=False):
    """Compile gc_compare.ts with ts-aot."""
    if not COMPILER.exists():
        print(f"ERROR: Compiler not found at {COMPILER}")
        print("Run: cmake --build build --config Release")
        return False

    # Ensure tmp directory exists
    AOT_EXE.parent.mkdir(parents=True, exist_ok=True)

    cmd = [str(COMPILER), str(BENCHMARK_TS), "-o", str(AOT_EXE)]
    if verbose:
        print(f"  Compiling: {' '.join(cmd)}")

    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        print(f"ERROR: ts-aot compilation failed:")
        print(result.stderr)
        return False

    if not AOT_EXE.exists():
        print(f"ERROR: Expected output {AOT_EXE} not found")
        return False

    return True


# ============================================================================
# Run benchmarks
# ============================================================================

def run_aot(gc_verbose=False, verbose=False):
    """Run the ts-aot compiled benchmark."""
    if not AOT_EXE.exists():
        return None

    env = os.environ.copy()
    if gc_verbose:
        env["TS_GC_VERBOSE"] = "1"

    try:
        result = subprocess.run(
            [str(AOT_EXE)],
            capture_output=True, text=True, timeout=300,
            env=env
        )
    except subprocess.TimeoutExpired:
        print("  WARNING: ts-aot benchmark timed out (300s)")
        return None

    if verbose:
        print("--- ts-aot raw output ---")
        print(result.stdout[:2000])
        if result.stderr:
            print("--- stderr ---")
            print(result.stderr[:1000])

    if result.returncode != 0:
        print(f"  WARNING: ts-aot exited with code {result.returncode}")
        if result.stderr:
            print(f"  stderr: {result.stderr[:500]}")
        return None

    return parse_output(result.stdout)


def run_node(mode="fair", verbose=False):
    """Run the Node.js benchmark with GC instrumentation."""
    if not NODE_RUNNER.exists():
        print(f"ERROR: Node runner not found at {NODE_RUNNER}")
        return None

    node_args = V8_MODES.get(mode, V8_MODES["fair"])
    cmd = ["node"] + node_args + [str(NODE_RUNNER)]

    if verbose:
        print(f"  Running: {' '.join(cmd)}")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True, text=True, timeout=300
        )
    except subprocess.TimeoutExpired:
        print("  WARNING: Node.js benchmark timed out (300s)")
        return None

    if verbose:
        print("--- Node.js raw output ---")
        print(result.stdout[:2000])
        if result.stderr:
            print("--- stderr ---")
            print(result.stderr[:1000])

    if result.returncode != 0:
        print(f"  WARNING: Node.js exited with code {result.returncode}")
        if result.stderr:
            print(f"  stderr: {result.stderr[:500]}")
        return None

    return parse_output(result.stdout)


# ============================================================================
# Multi-iteration runner
# ============================================================================

def run_multiple(runner_fn, iterations, **kwargs):
    """Run a benchmark multiple times and collect all results."""
    results = []
    for i in range(iterations):
        r = runner_fn(**kwargs)
        if r is not None:
            results.append(r)
    return results


def aggregate_results(results_list):
    """Aggregate multiple runs into min/avg/max per workload."""
    if not results_list:
        return None

    workload_names = list(results_list[0]["workloads"].keys())
    agg = {
        "runtime": results_list[0]["runtime"],
        "workloads": {},
        "gc_stats": {},
        "gc_summary": None,
        "heap_final_avg": statistics.mean(r["heap_final"] for r in results_list),
        "rss_final_avg": statistics.mean(r["rss_final"] for r in results_list),
    }

    for name in workload_names:
        walls = [r["workloads"][name]["wall"] for r in results_list if name in r["workloads"]]
        peaks = [r["workloads"][name]["peakHeap"] for r in results_list if name in r["workloads"]]
        heaps_after = [r["workloads"][name]["heapAfter"] for r in results_list if name in r["workloads"]]
        items = results_list[0]["workloads"][name].get("items", 0)

        agg["workloads"][name] = {
            "wall_min": min(walls) if walls else 0,
            "wall_avg": statistics.mean(walls) if walls else 0,
            "wall_max": max(walls) if walls else 0,
            "wall_stddev": statistics.stdev(walls) if len(walls) > 1 else 0,
            "peak_heap_avg": statistics.mean(peaks) if peaks else 0,
            "heap_after_avg": statistics.mean(heaps_after) if heaps_after else 0,
            "items": items,
        }

    # Aggregate GC stats (Node.js only)
    for name in workload_names:
        gc_counts = [r["gc_stats"][name]["count"] for r in results_list if name in r.get("gc_stats", {})]
        gc_totals = [r["gc_stats"][name]["totalMs"] for r in results_list if name in r.get("gc_stats", {})]
        gc_maxes = [r["gc_stats"][name]["maxPauseMs"] for r in results_list if name in r.get("gc_stats", {})]
        gc_pcts = [r["gc_stats"][name]["gcPct"] for r in results_list if name in r.get("gc_stats", {})]

        if gc_counts:
            agg["gc_stats"][name] = {
                "count_avg": statistics.mean(gc_counts),
                "totalMs_avg": statistics.mean(gc_totals),
                "maxPauseMs_avg": statistics.mean(gc_maxes),
                "gcPct_avg": statistics.mean(gc_pcts),
            }

    # Aggregate GC summary
    summaries = [r["gc_summary"] for r in results_list if r.get("gc_summary")]
    if summaries:
        agg["gc_summary"] = {
            "totalCollections_avg": statistics.mean(s["totalCollections"] for s in summaries),
            "totalGCTimeMs_avg": statistics.mean(s["totalGCTimeMs"] for s in summaries),
            "maxPauseMs_avg": statistics.mean(s["maxPauseMs"] for s in summaries),
            "gcPct_avg": statistics.mean(s["gcPct"] for s in summaries),
        }

    return agg


# ============================================================================
# Reporting
# ============================================================================

def fmt_ms(ms):
    if ms < 1:
        return f"{ms*1000:.1f}us"
    if ms < 1000:
        return f"{ms:.1f}ms"
    return f"{ms/1000:.2f}s"

def fmt_bytes(b):
    if b < 1024:
        return f"{b}B"
    if b < 1024 * 1024:
        return f"{b/1024:.1f}KB"
    if b < 1024 * 1024 * 1024:
        return f"{b/(1024*1024):.1f}MB"
    return f"{b/(1024*1024*1024):.2f}GB"


def print_comparison(aot_agg, node_agg, mode):
    """Print side-by-side comparison table."""

    print()
    print("=" * 100)
    print(f"  GC Comparison: ts-aot vs Node.js (mode={mode})")
    print("=" * 100)
    print()

    if aot_agg:
        print(f"  ts-aot runtime: {aot_agg['runtime'] or 'ts-aot'}")
    if node_agg:
        print(f"  Node.js runtime: {node_agg['runtime'] or 'unknown'}")
    print()

    # Get all workload names
    names = []
    if aot_agg:
        names = list(aot_agg["workloads"].keys())
    elif node_agg:
        names = list(node_agg["workloads"].keys())

    # --- Wall time comparison ---
    print("  Wall Time (lower is better)")
    print("  " + "-" * 90)
    print(f"  {'Workload':<25} {'ts-aot':>12} {'Node.js':>12} {'Ratio':>10} {'Winner':>10}")
    print("  " + "-" * 90)

    for name in names:
        aot_wall = aot_agg["workloads"][name]["wall_avg"] if aot_agg and name in aot_agg["workloads"] else None
        node_wall = node_agg["workloads"][name]["wall_avg"] if node_agg and name in node_agg["workloads"] else None

        aot_str = fmt_ms(aot_wall) if aot_wall is not None else "N/A"
        node_str = fmt_ms(node_wall) if node_wall is not None else "N/A"

        if aot_wall and node_wall and node_wall > 0:
            ratio = aot_wall / node_wall
            if ratio < 1:
                winner = "ts-aot"
                ratio_str = f"{1/ratio:.2f}x"
            else:
                winner = "Node.js"
                ratio_str = f"{ratio:.2f}x"
        else:
            ratio_str = "N/A"
            winner = ""

        print(f"  {name:<25} {aot_str:>12} {node_str:>12} {ratio_str:>10} {winner:>10}")

    print("  " + "-" * 90)
    print()

    # --- Peak heap comparison ---
    print("  Peak Heap (lower is better)")
    print("  " + "-" * 90)
    print(f"  {'Workload':<25} {'ts-aot':>12} {'Node.js':>12} {'Ratio':>10}")
    print("  " + "-" * 90)

    for name in names:
        aot_peak = aot_agg["workloads"][name]["peak_heap_avg"] if aot_agg and name in aot_agg["workloads"] else None
        node_peak = node_agg["workloads"][name]["peak_heap_avg"] if node_agg and name in node_agg["workloads"] else None

        aot_str = fmt_bytes(aot_peak) if aot_peak is not None else "N/A"
        node_str = fmt_bytes(node_peak) if node_peak is not None else "N/A"

        if aot_peak and node_peak and node_peak > 0:
            ratio = aot_peak / node_peak
            ratio_str = f"{ratio:.2f}x"
        else:
            ratio_str = "N/A"

        print(f"  {name:<25} {aot_str:>12} {node_str:>12} {ratio_str:>10}")

    print("  " + "-" * 90)
    print()

    # --- Node.js GC stats ---
    if node_agg and node_agg["gc_stats"]:
        print("  Node.js GC Statistics")
        print("  " + "-" * 90)
        print(f"  {'Workload':<25} {'Collections':>12} {'GC Time':>12} {'Max Pause':>12} {'GC %':>10}")
        print("  " + "-" * 90)

        for name in names:
            if name in node_agg["gc_stats"]:
                gs = node_agg["gc_stats"][name]
                print(f"  {name:<25} {gs['count_avg']:>12.0f} {fmt_ms(gs['totalMs_avg']):>12} {fmt_ms(gs['maxPauseMs_avg']):>12} {gs['gcPct_avg']:>9.1f}%")

        print("  " + "-" * 90)

        if node_agg["gc_summary"]:
            s = node_agg["gc_summary"]
            print(f"  {'TOTAL':<25} {s['totalCollections_avg']:>12.0f} {fmt_ms(s['totalGCTimeMs_avg']):>12} {fmt_ms(s['maxPauseMs_avg']):>12} {s['gcPct_avg']:>9.1f}%")

        print()

    # --- Throughput comparison ---
    print("  Throughput (items/sec, higher is better)")
    print("  " + "-" * 90)
    print(f"  {'Workload':<25} {'ts-aot':>15} {'Node.js':>15} {'Ratio':>10}")
    print("  " + "-" * 90)

    for name in names:
        aot_w = aot_agg["workloads"][name] if aot_agg and name in aot_agg["workloads"] else None
        node_w = node_agg["workloads"][name] if node_agg and name in node_agg["workloads"] else None

        aot_tput = aot_w["items"] / (aot_w["wall_avg"] / 1000) if aot_w and aot_w["wall_avg"] > 0 else None
        node_tput = node_w["items"] / (node_w["wall_avg"] / 1000) if node_w and node_w["wall_avg"] > 0 else None

        aot_str = f"{aot_tput:,.0f}" if aot_tput else "N/A"
        node_str = f"{node_tput:,.0f}" if node_tput else "N/A"

        if aot_tput and node_tput and node_tput > 0:
            ratio = aot_tput / node_tput
            ratio_str = f"{ratio:.2f}x"
        else:
            ratio_str = "N/A"

        print(f"  {name:<25} {aot_str:>15} {node_str:>15} {ratio_str:>10}")

    print("  " + "-" * 90)
    print()

    # --- Checksum verification ---
    print("  Checksum Verification")
    all_match = True
    for name in names:
        aot_cs = None
        node_cs = None
        if aot_agg and name in aot_agg["workloads"]:
            # checksums are in the raw results, not aggregated; skip verification for aggregated
            pass
        # Note: checksum verification is best done on single runs
    print("  (Run with --iterations 1 --verbose to verify checksums match)")
    print()


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Fair GC Comparison: ts-aot vs Node.js",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Modes:
  fair           V8 heap constrained to 256MB, semi-space 4MB (default)
  default        V8 defaults (~1.4GB heap) - what users experience
  no-concurrent  V8 heap constrained + no concurrent/incremental marking
                 (closest to ts-aot's stop-the-world collector)
"""
    )
    parser.add_argument("--mode", default="fair", choices=["fair", "default", "no-concurrent"])
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--aot-only", action="store_true")
    parser.add_argument("--node-only", action="store_true")
    parser.add_argument("--gc-verbose", action="store_true", help="Set TS_GC_VERBOSE=1 for ts-aot")
    parser.add_argument("--skip-build", action="store_true", help="Skip ts-aot compilation")
    args = parser.parse_args()

    print(f"GC Comparison Benchmark (mode={args.mode}, iterations={args.iterations})")
    print()

    aot_results = []
    node_results = []

    # --- ts-aot ---
    if not args.node_only:
        if not args.skip_build:
            print("Building ts-aot benchmark...")
            if not build_aot(verbose=args.verbose):
                print("  FAILED - skipping ts-aot runs")
            else:
                print("  OK")

        if AOT_EXE.exists():
            print(f"Running ts-aot ({args.iterations} iterations)...")
            for i in range(args.iterations):
                print(f"  Run {i+1}/{args.iterations}...", end="", flush=True)
                r = run_aot(gc_verbose=args.gc_verbose, verbose=args.verbose)
                if r:
                    aot_results.append(r)
                    # Show wall time for the first workload as progress indicator
                    first_wl = list(r["workloads"].keys())[0] if r["workloads"] else None
                    if first_wl:
                        print(f" {first_wl}={fmt_ms(r['workloads'][first_wl]['wall'])}", end="")
                    print(" OK")
                else:
                    print(" FAILED")

    # --- Node.js ---
    if not args.aot_only:
        print(f"Running Node.js ({args.iterations} iterations, mode={args.mode})...")
        v8_flags = V8_MODES[args.mode]
        print(f"  V8 flags: {' '.join(v8_flags)}")
        for i in range(args.iterations):
            print(f"  Run {i+1}/{args.iterations}...", end="", flush=True)
            r = run_node(mode=args.mode, verbose=args.verbose)
            if r:
                node_results.append(r)
                first_wl = list(r["workloads"].keys())[0] if r["workloads"] else None
                if first_wl:
                    print(f" {first_wl}={fmt_ms(r['workloads'][first_wl]['wall'])}", end="")
                print(" OK")
            else:
                print(" FAILED")

    # --- Aggregate and report ---
    aot_agg = aggregate_results(aot_results) if aot_results else None
    node_agg = aggregate_results(node_results) if node_results else None

    if not aot_agg and not node_agg:
        print("\nERROR: No results collected from either runtime")
        return 1

    print_comparison(aot_agg, node_agg, args.mode)
    return 0


if __name__ == "__main__":
    sys.exit(main())
