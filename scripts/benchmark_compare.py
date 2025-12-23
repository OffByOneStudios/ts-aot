import subprocess
import time
import os
import sys
import argparse
import json

def run_command(cmd, cwd=None, shell=False, capture=True):
    if shell:
        cmd_str = cmd if isinstance(cmd, str) else " ".join(cmd)
    else:
        cmd_str = " ".join(cmd)
        
    result = subprocess.run(cmd, capture_output=capture, text=True, cwd=cwd, shell=shell)
    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        return None
    return result.stdout

def extract_time(output):
    if not output:
        return None
    # Look for "Benchmark: Xms" or "Total Time: Xms"
    for line in output.splitlines():
        if "Benchmark:" in line and "ms" in line:
            try:
                return float(line.split(":")[1].strip().replace("ms", ""))
            except:
                pass
        if "Total Time:" in line and "ms" in line:
            try:
                return float(line.split(":")[1].strip().replace("ms", ""))
            except:
                pass
    return None

def main():
    parser = argparse.ArgumentParser(description="Compare ts-aot and Node.js performance.")
    parser.add_argument("--benchmarks", nargs="+", help="Specific benchmarks to run.")
    parser.add_argument("--config", default="Release", choices=["Debug", "Release"], help="Build configuration.")
    parser.add_argument("--iterations", type=int, default=1, help="Number of iterations to run.")
    args = parser.parse_known_args()[0]

    all_benchmarks = [
        "examples/sha256.ts",
        "examples/lru_cache.ts",
        "examples/json_transformer.ts",
        "examples/data_pipeline.ts",
        "examples/async_ping_pong.ts",
        "examples/raytracer.ts"
    ]

    benchmarks_to_run = args.benchmarks if args.benchmarks else all_benchmarks
    is_windows = sys.platform == "win32"

    print(f"{'Benchmark':<25} | {'Node.js (ms)':<15} | {'ts-aot (ms)':<15} | {'Speedup':<10}")
    print("-" * 75)

    # Ensure build is up to date
    print("Building ts-aot...", end="", flush=True)
    subprocess.run(["cmake", "--build", "build", "--config", args.config, "--target", "ts-aot"], capture_output=True)
    print(" Done.")

    # Find tsc
    tsc_path = os.path.abspath("scripts/node_modules/.bin/tsc")
    if is_windows: tsc_path += ".cmd"
    if not os.path.exists(tsc_path):
        # Fallback to npx
        tsc_cmd = ["npx.cmd" if is_windows else "npx", "-p", "typescript", "tsc"]
    else:
        tsc_cmd = [tsc_path]

    for ts_file in benchmarks_to_run:
        if not os.path.exists(ts_file):
            print(f"Skipping {ts_file} (not found)")
            continue

        base_name = os.path.splitext(os.path.basename(ts_file))[0]
        
        # 1. Run with Node.js
        js_file = os.path.abspath(f"build/{base_name}.js")
        os.makedirs("build", exist_ok=True)
        
        # Compile to JS
        run_command(tsc_cmd + [ts_file, "--outFile", js_file, "--target", "es2020", "--lib", "es2020,dom", "--skipLibCheck"], shell=is_windows)
        
        node_times = []
        if os.path.exists(js_file):
            for _ in range(args.iterations):
                node_output = run_command(["node", js_file])
                t = extract_time(node_output)
                if t is not None: node_times.append(t)
        
        node_time = sum(node_times) / len(node_times) if node_times else None

        # 2. Run with ts-aot
        aot_times = []
        # test_runner.py handles compilation and linking
        runner_path = os.path.abspath("scripts/test_runner.py")
        compile_output = run_command([sys.executable, runner_path, ts_file, "--config", args.config], shell=False)
        
        # Try to extract time from test_runner output first
        t = extract_time(compile_output)
        if t is not None:
            aot_times.append(t)
        
        # If we need more iterations, run the executable directly
        if len(aot_times) < args.iterations:
            exe_path = os.path.abspath(f"build/tests/{base_name}/{args.config}/test_app.exe")
            if os.path.exists(exe_path):
                for _ in range(args.iterations - len(aot_times)):
                    aot_output = run_command([exe_path])
                    t = extract_time(aot_output)
                    if t is not None: aot_times.append(t)
            else:
                if not aot_times:
                    print(f"Failed to find executable at {exe_path}")
        
        aot_time = sum(aot_times) / len(aot_times) if aot_times else None

        if node_time is not None and aot_time is not None:
            speedup = node_time / aot_time if aot_time > 0 else 0
            print(f"{base_name:<25} | {node_time:<15.2f} | {aot_time:<15.2f} | {speedup:<10.2f}x")
        else:
            node_str = f"{node_time:.2f}" if node_time is not None else "FAIL"
            aot_str = f"{aot_time:.2f}" if aot_time is not None else "FAIL"
            print(f"{base_name:<25} | {node_str:<15} | {aot_str:<15} | N/A")

if __name__ == "__main__":
    main()
