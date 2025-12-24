import subprocess
import time
import asyncio
import aiohttp
import os
import sys
import statistics

async def run_load_test(url, duration=10, concurrency=50):
    print(f"Running load test against {url} (concurrency={concurrency}, duration={duration}s)...")
    
    results = []
    start_time = time.time()
    end_time = start_time + duration
    
    async with aiohttp.ClientSession() as session:
        async def worker():
            while time.time() < end_time:
                req_start = time.time()
                try:
                    async with session.get(url, timeout=2) as response:
                        await response.read()
                        if response.status == 200:
                            results.append(time.time() - req_start)
                        else:
                            print(f"Error: {response.status}")
                except Exception as e:
                    # print(f"Request failed: {e}")
                    pass

        tasks = [asyncio.create_task(worker()) for _ in range(concurrency)]
        await asyncio.gather(*tasks)

    total_time = time.time() - start_time
    if not results:
        return {"rps": 0, "avg_latency": 0, "p99_latency": 0, "total_requests": 0}
        
    rps = len(results) / total_time
    avg_latency = statistics.mean(results) * 1000
    p99_latency = statistics.quantiles(results, n=100)[98] * 1000 if len(results) > 1 else avg_latency
    
    return {
        "rps": rps,
        "avg_latency": avg_latency,
        "p99_latency": p99_latency,
        "total_requests": len(results)
    }

def start_server(cmd):
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(2)
    if process.poll() is not None:
        stdout, stderr = process.communicate()
        print(f"Failed to start server: {cmd}")
        print(f"STDOUT: {stdout}")
        print(f"STDERR: {stderr}")
        return None
    return process

async def main():
    aot_exe = os.path.abspath("examples/http-server/http-server.exe")
    lto_exe = os.path.abspath("examples/http-server/http-server-lto.exe")
    node_script = os.path.abspath("examples/http-server/http-server.js")
    
    # 1. Benchmark AOT (Non-LTO)
    print("--- Benchmarking AOT Server (Non-LTO) ---")
    aot_proc = start_server([aot_exe])
    if not aot_proc: return
    
    aot_results = await run_load_test("http://127.0.0.1:8080/json")
    aot_proc.terminate()
    aot_proc.wait()

    # 2. Benchmark AOT (LTO)
    lto_results = None
    if os.path.exists(lto_exe):
        print("\n--- Benchmarking AOT Server (LTO) ---")
        lto_proc = start_server([lto_exe])
        if lto_proc:
            lto_results = await run_load_test("http://127.0.0.1:8080/json")
            lto_proc.terminate()
            lto_proc.wait()
    
    # 3. Benchmark Node.js
    print("\n--- Benchmarking Node.js Server ---")
    node_proc = start_server(["node", node_script])
    if not node_proc: return
    
    node_results = await run_load_test("http://127.0.0.1:8081/json")
    node_proc.terminate()
    node_proc.wait()
    
    # 4. Compare
    print("\n" + "="*60)
    print(f"{'Metric':<20} | {'AOT':<10} | {'AOT (LTO)':<10} | {'Node.js':<10}")
    print("-" * 60)
    lto_rps = f"{lto_results['rps']:>10.2f}" if lto_results else "N/A"
    lto_avg = f"{lto_results['avg_latency']:>10.2f}" if lto_results else "N/A"
    lto_p99 = f"{lto_results['p99_latency']:>10.2f}" if lto_results else "N/A"
    lto_total = f"{lto_results['total_requests']:>10}" if lto_results else "N/A"

    print(f"{'Requests/sec':<20} | {aot_results['rps']:>10.2f} | {lto_rps} | {node_results['rps']:>10.2f}")
    print(f"{'Avg Latency (ms)':<20} | {aot_results['avg_latency']:>10.2f} | {lto_avg} | {node_results['avg_latency']:>10.2f}")
    print(f"{'P99 Latency (ms)':<20} | {aot_results['p99_latency']:>10.2f} | {lto_p99} | {node_results['p99_latency']:>10.2f}")
    print(f"{'Total Requests':<20} | {aot_results['total_requests']:>10} | {lto_total} | {node_results['total_requests']:>10}")
    print("="*60)

if __name__ == "__main__":
    asyncio.run(main())
