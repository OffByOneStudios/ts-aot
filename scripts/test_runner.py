import os
import sys
import subprocess
import json
import shutil

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
        print(result.stdout, flush=True)
        print(result.stderr, flush=True)
        sys.exit(1)
    
    if result.stderr:
        print("STDERR:", result.stderr, flush=True)

    return result.stdout

def main():
    if len(sys.argv) < 2:
        print("Usage: python test_runner.py <input.ts>", flush=True)
        sys.exit(1)

    input_ts = os.path.abspath(sys.argv[1])
    base_name = os.path.splitext(os.path.basename(input_ts))[0]
    build_dir = os.path.abspath("build")
    
    # Paths
    dump_ast_script = os.path.abspath("scripts/dump_ast.js")
    compiler_exe = os.path.join(build_dir, "src", "compiler", "Debug", "ts-aot.exe")
    runtime_lib = os.path.join(build_dir, "src", "runtime", "Debug", "tsruntime.lib")
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
    run_command(f"node {dump_ast_script} {input_ts} > {json_file}", timeout=10)

    # Step 2: Compile to Object Code
    print("--- Step 2: Compile ---", flush=True)
    compiler_output = run_command([compiler_exe, json_file, "-o", obj_file, "-d"], shell=False, timeout=10)
    print(compiler_output, flush=True)

    # Step 3: Link (using CMake)
    print("--- Step 3: Link ---", flush=True)
    
    # Configure
    config_cmd = ["cmake", "-S", cmake_src, "-B", test_build_dir_cmake, 
                  f"-DCMAKE_PREFIX_PATH={vcpkg_installed_cmake}", 
                  f"-DTEST_OBJ={obj_file_cmake}", 
                  f"-DTSRUNTIME_LIB={runtime_lib_cmake}"]
    run_command(config_cmd, shell=False, timeout=30)
    
    # Build
    build_cmd = ["cmake", "--build", test_build_dir_cmake, "--config", "Debug"]
    run_command(build_cmd, shell=False, timeout=30)

    # Step 4: Run
    print("--- Step 4: Run ---", flush=True)
    exe_path = os.path.join(test_build_dir, "Debug", "test_app.exe")
    
    # Add vcpkg bin to PATH for DLLs
    vcpkg_bin = os.path.join(build_dir, "vcpkg_installed", "x64-windows", "bin")
    vcpkg_debug_bin = os.path.join(build_dir, "vcpkg_installed", "x64-windows", "debug", "bin")
    
    env = os.environ.copy()
    env["PATH"] = vcpkg_bin + os.pathsep + vcpkg_debug_bin + os.pathsep + env["PATH"]
    
    output = run_command([exe_path], shell=False, env=env, timeout=5)
    print("Output:", flush=True)
    print(output, flush=True)

    # Cleanup
    # os.remove(json_file)
    # os.remove(obj_file)
    # shutil.rmtree(test_build_dir)

if __name__ == "__main__":
    main()
