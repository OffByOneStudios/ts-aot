import subprocess
import time
import os
import sys
import json
import argparse

def run_command(cmd, shell=False):
    result = subprocess.run(cmd, capture_output=True, text=True, shell=shell)
    if result.returncode != 0:
        print(f"Error running command: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
        print(result.stderr)
        return None
    return result.stdout

def benchmark(ts_file, iterations=1):
    print(f"\n--- Benchmarking: {ts_file} ---")
    
    # 1. Compile with ts-aot
    base_name = os.path.splitext(os.path.basename(ts_file))[0]
    json_ast = ts_file.replace(".ts", ".json")
    obj_file = ts_file.replace(".ts", ".obj")
    exe_file = f"./build/Release/{base_name}_bench.exe"
    
    # Generate AST
    run_command(["node", "./scripts/dump_ast.js", ts_file, json_ast])
    
    # Compile
    ts_aot = "./build/src/compiler/Release/ts-aot.exe"
    runtime_bc = "./build/tsruntime.bc"
    
    compile_cmd = [
        ts_aot, json_ast, 
        "-o", obj_file, 
        "-O3", 
        "--runtime-bc", runtime_bc
    ]
    
    print("Compiling with ts-aot...")
    run_command(compile_cmd)
    
    # Build the final executable (using the existing CMake target if possible, or manual link)
    # For simplicity in this script, we assume the user has built the 'raytracer_bench' style targets
    # or we can try to link it manually if we had a generic linker script.
    # Since we have raytracer_bench.exe already, let's try to run it if it exists.
    
    aot_times = []
    if os.path.exists(exe_file):
        print(f"Running ts-aot binary: {exe_file}")
        for _ in range(iterations):
            start = time.time()
            output = run_command([exe_file])
            end = time.time()
            if output:
                # Try to parse "Total Time: Xms" from output
                for line in output.splitlines():
                    if "Total Time:" in line:
                        ms = float(line.split(":")[1].strip().replace("ms", ""))
                        aot_times.append(ms)
                        break
                else:
                    aot_times.append((end - start) * 1000)
    else:
        print(f"Warning: {exe_file} not found. Skipping AOT execution.")

    # 2. Run with Node.js
    print("Running with Node.js...")
    node_times = []
    for _ in range(iterations):
        start = time.time()
        output = run_command(["node", ts_file])
        end = time.time()
        if output:
            for line in output.splitlines():
                if "Total Time:" in line:
                    ms = float(line.split(":")[1].strip().replace("ms", ""))
                    node_times.append(ms)
                    break
            else:
                node_times.append((end - start) * 1000)

    # 3. Report
    if aot_times and node_times:
        avg_aot = sum(aot_times) / len(aot_times)
        avg_node = sum(node_times) / len(node_times)
        speedup = avg_node / avg_aot
        
        print("\nResults:")
        print(f"  Node.js: {avg_node:.2f}ms")
        print(f"  ts-aot:  {avg_aot:.2f}ms")
        print(f"  Ratio:   {speedup:.2f}x {'faster' if speedup > 1 else 'slower'}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("file", help="TS file to benchmark")
    parser.add_argument("--iterations", type=int, default=3)
    args = parser.parse_args()
    
    benchmark(args.file, args.iterations)
