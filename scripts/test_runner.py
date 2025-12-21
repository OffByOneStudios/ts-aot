import os
import sys
import subprocess
import json
import shutil
import argparse

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
        print("STDERR:", result.stderr, flush=True)

    return result.stdout

def main():
    parser = argparse.ArgumentParser(description="Compile and run a TypeScript file.")
    parser.add_argument("input_ts", help="The input TypeScript file to compile and run.")
    parser.add_argument("--timeout", type=int, default=60, help="Timeout for the execution step in seconds (default: 60).")
    parser.add_argument("--config", default="Debug", choices=["Debug", "Release"], help="Build configuration (default: Debug).")
    
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
    run_command(f"node {dump_ast_script} {input_ts} > {json_file}", timeout=10)

    # Step 2: Compile to Object Code
    print("--- Step 2: Compile ---", flush=True)
    compiler_output = run_command([compiler_exe, json_file, "-o", obj_file, "-d"], shell=False, timeout=60)
    print(compiler_output, flush=True)

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
