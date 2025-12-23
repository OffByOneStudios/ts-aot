import os
import sys
import subprocess
import json
import shutil
import argparse
import re

def run_command(cmd, shell=True, timeout=None, env=None):
    print(f"Running: {cmd}", flush=True)
    try:
        result = subprocess.run(cmd, shell=shell, capture_output=True, text=True, env=env, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        print(f"Error: Command timed out after {timeout} seconds", flush=True)
        if e.stdout: print("STDOUT:", e.stdout, flush=True)
        if e.stderr: print("STDERR:", e.stderr, flush=True)
        sys.exit(1)

    if result.returncode != 0:
        print(f"Error running command: {cmd}", flush=True)
        print(f"Return code: {result.returncode}", flush=True)
        if result.stdout:
            print("STDOUT:", flush=True)
            print(result.stdout, flush=True)
        if result.stderr:
            print("STDERR:", flush=True)
            print(result.stderr, flush=True)
        sys.exit(1)
    
    if result.stderr:
        # Filter out common LLVM warnings or noise if needed
        print("STDERR:", result.stderr, flush=True)

    return result.stdout

def verify_output(input_ts, output_text, prefix='CHECK'):
    patterns = []
    with open(input_ts, 'r', encoding='utf-8') as f:
        for line in f:
            # Match // PREFIX: pattern or // PREFIX-NEXT: pattern
            match = re.search(r'//\s*(' + re.escape(prefix) + r'(?:-NEXT|-NOT|-LABEL)?):\s*(.*)', line)
            if match:
                patterns.append((match.group(1), match.group(2).strip()))
    
    if not patterns:
        return True

    print(f"--- Verifying {prefix} ({len(patterns)} patterns) ---", flush=True)
    
    lines = output_text.splitlines()
    current_line_idx = 0
    
    for i, (p_type, p_pattern) in enumerate(patterns):
        found = False
        
        # Support {{regex}} syntax and be flexible with whitespace
        parts = re.split(r'(\{\{.*?\}\})', p_pattern)
        regex_parts = []
        for part in parts:
            if part.startswith('{{') and part.endswith('}}'):
                regex_parts.append(part[2:-2])
            else:
                escaped = re.escape(part)
                # Replace escaped spaces with '\s*' to be flexible with whitespace
                escaped = escaped.replace(r'\ ', r'\s*')
                regex_parts.append(escaped)
        processed_pattern = "".join(regex_parts)
        
        if p_type == prefix or p_type == prefix + '-LABEL':
            for j in range(current_line_idx, len(lines)):
                if re.search(processed_pattern, lines[j]):
                    current_line_idx = j + 1
                    found = True
                    break
        elif p_type == prefix + '-NEXT':
            if current_line_idx < len(lines):
                if re.search(processed_pattern, lines[current_line_idx]):
                    current_line_idx += 1
                    found = True
                else:
                    print(f"{prefix}-NEXT failed at pattern {i+1}: {p_pattern}")
                    print(f"  Expected on line {current_line_idx + 1}")
                    print(f"  Actual line: {lines[current_line_idx].strip()}")
            else:
                print(f"{prefix}-NEXT failed: End of output reached")
        elif p_type == prefix + '-NOT':
            # Find the next CHECK/LABEL to define the range
            end_idx = len(lines)
            for k in range(i + 1, len(patterns)):
                if patterns[k][0] in [prefix, prefix + '-LABEL']:
                    break
            
            for j in range(current_line_idx, end_idx):
                if re.search(processed_pattern, lines[j]):
                    print(f"{prefix}-NOT failed: Found forbidden pattern '{p_pattern}' on line {j+1}")
                    print(f"  Line: {lines[j].strip()}")
                    return False
            found = True # CHECK-NOT "succeeds" if it doesn't find anything
            
        if not found:
            print(f"Verification FAILED: Could not find {p_type}: {p_pattern}")
            # Print some context
            start = max(0, current_line_idx - 5)
            end = min(len(lines), current_line_idx + 10)
            print("Context (around current position):")
            for k in range(start, end):
                prefix_marker = ">>" if k == current_line_idx else "  "
                print(f"{prefix_marker} {lines[k]}")
            return False
            
    print(f"Verification {prefix} SUCCESS", flush=True)
    return True

def verify_performance(input_ts, output_text, config):
    baseline = None
    with open(input_ts, 'r', encoding='utf-8') as f:
        for line in f:
            match = re.search(r'//\s*PERF-BASELINE:\s*([\d.]+)\s*ms', line)
            if match:
                baseline = float(match.group(1))
                break
    
    if baseline is None:
        return True

    print(f"--- Verifying Performance (Baseline: {baseline}ms) ---", flush=True)
    
    if config != "Release":
        print(f"  Skipping performance check: Current config is '{config}', but performance guards require 'Release'.")
        return True

    # Extract actual time: "Average Time: 0.007ms"
    match = re.search(r'Average Time:\s*([\d.]+)\s*ms', output_text)
    if not match:
        print("  Error: Could not find 'Average Time: ...ms' in output.")
        return False
    
    actual = float(match.group(1))
    print(f"  Actual Time: {actual}ms")

    # Allow 5% regression
    threshold = baseline * 1.05
    if actual > threshold:
        print(f"  Verification FAILED: Performance regressed!")
        print(f"  Baseline: {baseline}ms")
        print(f"  Actual:   {actual}ms")
        print(f"  Threshold: {threshold:.4f}ms (5% margin)")
        return False
    
    print(f"  Performance SUCCESS: {actual}ms <= {threshold:.4f}ms")
    return True

def main():
    parser = argparse.ArgumentParser(description="Compile and run a TypeScript file.")
    parser.add_argument("input_ts", help="The input TypeScript file to compile and run.")
    parser.add_argument("--timeout", type=int, default=60, help="Timeout for the execution step in seconds (default: 60).")
    parser.add_argument("--config", default="Debug", choices=["Debug", "Release"], help="Build configuration (default: Debug).")
    parser.add_argument("--compiler-opts", default="", help="Extra options to pass to the ts-aot compiler.")
    
    # Use parse_known_args to allow passing extra arguments to the test app
    args, extra_args = parser.parse_known_args()

    input_ts = os.path.abspath(args.input_ts)
    base_name = os.path.splitext(os.path.basename(input_ts))[0]
    build_dir = os.path.abspath("build")
    config = args.config
    
    # Paths
    dump_ast_script = os.path.abspath("scripts/dump_ast.js")
    
    # Use specified config
    compiler_exe = os.path.join(build_dir, "src", "compiler", config, "ts-aot.exe")
    runtime_lib = os.path.join(build_dir, "src", "runtime", config, "tsruntime.lib")
    vcpkg_installed = os.path.join(build_dir, "vcpkg_installed", "x64-windows")
    
    # Intermediate files
    json_file = os.path.join(os.path.dirname(input_ts), f"{base_name}.json")
    obj_file = os.path.join(os.path.dirname(input_ts), f"{base_name}.obj")
    
    # Test build dir
    test_build_dir = os.path.abspath(f"build/tests/{base_name}")
    os.makedirs(test_build_dir, exist_ok=True)

    # Normalize paths for CMake
    cmake_src = os.path.abspath("scripts/test_runner").replace(os.sep, '/')
    test_build_dir_cmake = test_build_dir.replace(os.sep, '/')
    vcpkg_installed_cmake = vcpkg_installed.replace(os.sep, '/')
    obj_file_cmake = obj_file.replace(os.sep, '/')
    runtime_lib_cmake = runtime_lib.replace(os.sep, '/')

    # Step 1: Dump AST
    print("--- Step 1: Dump AST ---", flush=True)
    run_command(f"node {dump_ast_script} {input_ts} {json_file}", timeout=10)

    # Step 2: Compile to Object Code
    print("--- Step 2: Compile ---", flush=True)
    compile_cmd = [compiler_exe, json_file, "-o", obj_file, "-d", "--dump-ir", "--dump-types"]
    if args.compiler_opts:
        compile_cmd.extend(args.compiler_opts.split())
    compiler_output = run_command(compile_cmd, shell=False, timeout=60)
    
    # Verify IR if patterns exist
    if not verify_output(input_ts, compiler_output, prefix='CHECK'):
        sys.exit(1)
    
    # Verify Types if patterns exist
    if not verify_output(input_ts, compiler_output, prefix='TYPE-CHECK'):
        sys.exit(1)

    # Step 3: Link (using CMake)
    print("--- Step 3: Link ---", flush=True)
    
    # Configure
    config_cmd = ["cmake", "-S", cmake_src, "-B", test_build_dir_cmake, 
                  f"-DCMAKE_PREFIX_PATH={vcpkg_installed_cmake}", 
                  f"-DTEST_OBJ={obj_file_cmake}", 
                  f"-DTSRUNTIME_LIB={runtime_lib_cmake}"]
    
    if "VCPKG_ROOT" in os.environ:
        vcpkg_root = os.environ["VCPKG_ROOT"].replace(os.sep, '/')
        toolchain = f"{vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
        config_cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")
        # Ensure we use the same triplet
        config_cmd.append("-DVCPKG_TARGET_TRIPLET=x64-windows")

    run_command(config_cmd, shell=False, timeout=30)
    
    # Build
    build_cmd = ["cmake", "--build", test_build_dir_cmake, "--config", config]
    run_command(build_cmd, shell=False, timeout=30)

    # Step 4: Copy DLLs
    print("--- Step 4: Copy DLLs ---", flush=True)
    exe_dir = os.path.join(test_build_dir, config)
    
    # Copy DLLs from vcpkg
    # Always copy Release DLLs first (as fallback or if needed)
    vcpkg_bin_release = os.path.join(build_dir, "vcpkg_installed", "x64-windows", "bin")
    if os.path.exists(vcpkg_bin_release):
        for item in os.listdir(vcpkg_bin_release):
            if item.endswith(".dll"):
                shutil.copy2(os.path.join(vcpkg_bin_release, item), exe_dir)

    # Then copy Debug DLLs if in Debug mode (overwriting Release ones if same name)
    if config == "Debug":
        vcpkg_bin_debug = os.path.join(build_dir, "vcpkg_installed", "x64-windows", "debug", "bin")
        if os.path.exists(vcpkg_bin_debug):
            for item in os.listdir(vcpkg_bin_debug):
                if item.endswith(".dll"):
                    shutil.copy2(os.path.join(vcpkg_bin_debug, item), exe_dir)

    # Step 5: Run
    print("--- Step 5: Run ---", flush=True)
    exe_path = os.path.join(exe_dir, "test_app.exe")
    
    # Add vcpkg bin to PATH
    env = os.environ.copy()
    # Use the appropriate bin dir for PATH
    vcpkg_bin_path = vcpkg_bin_release
    if config == "Debug" and os.path.exists(os.path.join(build_dir, "vcpkg_installed", "x64-windows", "debug", "bin")):
        vcpkg_bin_path = os.path.join(build_dir, "vcpkg_installed", "x64-windows", "debug", "bin")
    
    if os.path.exists(vcpkg_bin_path):
        env["PATH"] = vcpkg_bin_path + os.pathsep + env.get("PATH", "")

    try:
        result = subprocess.run([exe_path] + extra_args, capture_output=True, text=True, timeout=args.timeout, env=env)
        if result.returncode != 0:
            print(f"Error running command: {exe_path}")
            print(f"Return code: {result.returncode}")
            print(f"STDOUT:\n{result.stdout}")
            print(f"STDERR:\n{result.stderr}")
            sys.exit(1)
        print("Output:", flush=True)
        print(result.stdout, flush=True)
        
        # Verify performance if baseline exists
        if not verify_performance(input_ts, result.stdout, config):
            sys.exit(1)

        return result.stdout
    except subprocess.TimeoutExpired:
        print(f"Command timed out after {args.timeout} seconds")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

    # Cleanup
    # os.remove(json_file)
    # os.remove(obj_file)
    # shutil.rmtree(test_build_dir)

if __name__ == "__main__":
    main()
