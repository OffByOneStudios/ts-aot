import subprocess
import time
import http.client
import os
import sys

def test_server():
    server_exe = os.path.abspath("build/Debug/test_app.exe")
    if not os.path.exists(server_exe):
        # Try alternative location
        server_exe = os.path.abspath("build/tests/http-server/Debug/test_app.exe")
    
    if not os.path.exists(server_exe):
        print(f"Error: Server executable not found")
        return False

    print(f"Starting server: {server_exe}")
    process = subprocess.Popen([server_exe], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    # Give the server a moment to start
    time.sleep(2)
    
    if process.poll() is not None:
        print("Error: Server failed to start or exited immediately")
        stdout, stderr = process.communicate()
        print(f"STDOUT: {stdout}")
        print(f"STDERR: {stderr}")
        return False

    success = True
    try:
        # Test 1: Root path
        print("Testing GET / ...")
        conn = http.client.HTTPConnection("127.0.0.1", 8080)
        conn.request("GET", "/", headers={"User-Agent": "TestAgent"})
        res = conn.getresponse()
        print(f"Status: {res.status}")
        data = res.read().decode()
        print(f"Response: {data}")
        if res.status != 404 or "Not Found" not in data:
            print("Error: Expected 404 Not Found for /")
            success = False
        conn.close()

        # Test 2: /json path
        print("\nTesting GET /json ...")
        conn = http.client.HTTPConnection("127.0.0.1", 8080)
        conn.request("GET", "/json", headers={"User-Agent": "TestAgent"})
        res = conn.getresponse()
        print(f"Status: {res.status}")
        data = res.read().decode()
        print(f"Response: {data}")
        if res.status != 200 or "Hello, World!" not in data:
            print("Error: Expected 200 OK with JSON for /json")
            success = False
        conn.close()

    except Exception as e:
        print(f"Error during request: {e}")
        success = False
    finally:
        print("\nShutting down server...")
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
        
        stdout, stderr = process.communicate()
        print("Server output:")
        print(stdout)
        if stderr:
            print("Server errors:")
            print(stderr)

    return success

if __name__ == "__main__":
    if test_server():
        print("\nHTTP Server tests PASSED")
        sys.exit(0)
    else:
        print("\nHTTP Server tests FAILED")
        sys.exit(1)
