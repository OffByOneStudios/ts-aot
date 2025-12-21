import subprocess
import time
import os
import sys
import shutil

def run_command(cmd, cwd=None, shell=False):
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, shell=shell)
    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        return result.stdout + "\n" + result.stderr
    return result.stdout

def main():
    ts_file = "examples/raytracer.ts"
    base_name = "raytracer"
    is_windows = sys.platform == "win32"
    
    # 1. Compile and Run with ts-aot
    print("--- Running with ts-aot ---")
    # Ensure build is up to date
    subprocess.run(["cmake", "--build", "build", "--config", "Release"])
    
    start_time = time.time()
    # test_runner.py handles AST dumping, compilation, linking, and execution
    aot_output = run_command([sys.executable, "scripts/test_runner.py", ts_file, "--config", "Release"])
    aot_time = time.time() - start_time
    print(aot_output)
    
    # 2. Run with Node.js (V8)
    print("\n--- Running with Node.js (V8) ---")
    # Compile to JS first
    run_command(["npx.cmd" if is_windows else "npx", "-p", "typescript", "tsc", ts_file, "--outFile", f"{base_name}.js", "--target", "es2020", "--lib", "es2020,dom"], shell=is_windows)
    
    start_time = time.time()
    v8_output = run_command(["node", f"{base_name}.js"])
    v8_time = time.time() - start_time
    print(v8_output)

    print("\n--- Comparison Results ---")
    print(f"ts-aot Total Time (including startup): {aot_time:.4f}s")
    print(f"Node.js Total Time (including startup): {v8_time:.4f}s")
    
    # Extract internal benchmark times if possible
    def extract_avg(output):
        for line in output.splitlines():
            if "Average Time:" in line:
                return line.split(":")[1].strip()
        return "N/A"

    print(f"ts-aot Internal Avg: {extract_avg(aot_output)}")
    print(f"Node.js Internal Avg: {extract_avg(v8_output)}")

if __name__ == "__main__":
    main()
