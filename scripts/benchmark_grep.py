import subprocess
import time
import os
import sys

def run_command(cmd, shell=False):
    start = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True, shell=shell)
    end = time.time()
    return end - start, result.stdout, result.stderr

def main():
    is_windows = sys.platform == "win32"
    ts_file = "examples/ts-grep/ts-grep.ts"
    aot_exe = os.path.abspath("examples/ts-grep/ts-grep.exe")
    search_dir = "src"
    pattern = "llvm"
    iterations = 5

    print(f"Benchmarking ts-grep on directory: {search_dir} with pattern: {pattern}")
    print(f"Running {iterations} iterations for runtime...")

    # 1. Node.js (V8)
    print("\n--- Node.js (V8) ---")
    js_file = "examples/ts-grep/ts-grep.js"
    # Compile to JS
    tsc_path = os.path.abspath("scripts/node_modules/.bin/tsc.cmd" if is_windows else "scripts/node_modules/.bin/tsc")
    subprocess.run([tsc_path, ts_file, "--target", "es2020", "--module", "commonjs", "--esModuleInterop", "true", "--skipLibCheck", "true"], shell=is_windows)
    
    if not os.path.exists(js_file):
        print(f"Error: {js_file} not found. Compilation failed.")
        return

    # Measure Startup (Time to run with no args)
    startup_v8, _, _ = run_command(["node", js_file])
    print(f"Startup Time (no args): {startup_v8:.4f}s")

    # Measure Runtime
    v8_times = []
    for i in range(iterations):
        t, stdout_v8, _ = run_command(["node", js_file, pattern, search_dir])
        v8_times.append(t)
    
    runtime_v8 = sum(v8_times) / iterations
    print(f"Average Runtime: {runtime_v8:.4f}s")
    results_v8 = len(stdout_v8.strip().split('\n')) if stdout_v8.strip() else 0
    print(f"Results found: {results_v8}")

    # 2. ts-aot
    print("\n--- ts-aot ---")
    if not os.path.exists(aot_exe):
        print(f"Error: {aot_exe} not found. Build it first.")
        return

    # Measure Startup (Time to run with no args)
    startup_aot, _, _ = run_command([aot_exe])
    print(f"Startup Time (no args): {startup_aot:.4f}s")

    # Measure Runtime
    aot_times = []
    for i in range(iterations):
        t, stdout_aot, _ = run_command([aot_exe, "dummy", pattern, search_dir])
        aot_times.append(t)
    
    runtime_aot = sum(aot_times) / iterations
    print(f"Average Runtime: {runtime_aot:.4f}s")
    results_aot = len(stdout_aot.strip().split('\n')) if stdout_aot.strip() else 0
    print(f"Results found: {results_aot}")

    print("\n--- Comparison ---")
    print(f"Startup Speedup: {startup_v8 / startup_aot:.2f}x")
    print(f"Runtime Speedup: {runtime_v8 / runtime_aot:.2f}x")

if __name__ == "__main__":
    main()
