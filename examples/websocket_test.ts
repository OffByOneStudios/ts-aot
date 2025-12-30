// WebSocket API Test
// Tests basic WebSocket class structure and API (RFC 6455)

let tests_passed = 0;
let tests_failed = 0;

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("✓ " + name);
        tests_passed = tests_passed + 1;
    } else {
        console.log("✗ " + name);
        tests_failed = tests_failed + 1;
    }
}

// Test 1: WebSocket constructor and URL property
console.log("--- WebSocket Constructor Tests ---");
const ws = new WebSocket("ws://localhost:8080/chat");
test("WebSocket constructor creates instance", ws !== null);

// Test 2: readyState starts at CONNECTING (0)
test("Initial readyState is CONNECTING (0)", ws.readyState === 0);

// Test 3: URL property is set correctly  
test("URL property matches constructor argument", ws.url === "ws://localhost:8080/chat");

// Test 4: Protocol starts empty
test("Protocol is initially empty", ws.protocol === "");

// Test 5: Extensions starts empty
test("Extensions is initially empty", ws.extensions === "");

// Test 6: binaryType defaults to blob
test("binaryType defaults to 'blob'", ws.binaryType === "blob");

// Test 7: bufferedAmount starts at 0
test("bufferedAmount starts at 0", ws.bufferedAmount === 0);

// Test 8: Ready state constants exist
console.log("--- WebSocket Constants Tests ---");
test("CONNECTING constant is 0", ws.CONNECTING === 0);
test("OPEN constant is 1", ws.OPEN === 1);
test("CLOSING constant is 2", ws.CLOSING === 2);
test("CLOSED constant is 3", ws.CLOSED === 3);

// Test 9: Event handlers can be set
console.log("--- WebSocket Event Handler Tests ---");
let openHandlerSet = false;
let messageHandlerSet = false;
let closeHandlerSet = false;
let errorHandlerSet = false;

ws.onopen = () => { openHandlerSet = true; };
ws.onmessage = (event: any) => { messageHandlerSet = true; };
ws.onclose = (event: any) => { closeHandlerSet = true; };
ws.onerror = (error: any) => { errorHandlerSet = true; };

test("onopen handler can be set", ws.onopen !== null);
test("onmessage handler can be set", ws.onmessage !== null);
test("onclose handler can be set", ws.onclose !== null);
test("onerror handler can be set", ws.onerror !== null);

// Test 10: binaryType can be changed
ws.binaryType = "arraybuffer";
test("binaryType can be set to 'arraybuffer'", ws.binaryType === "arraybuffer");

// Test 11: Methods exist and can be called (won't actually send because not connected)
console.log("--- WebSocket Methods Tests ---");

// Note: These methods won't do anything since we're not actually connected
// but we're testing that they exist and don't crash
ws.send("test message");
test("send() method exists and can be called", true);

ws.ping("ping data");
test("ping() method exists and can be called", true);

ws.pong("pong data");
test("pong() method exists and can be called", true);

// Note: close() changes the readyState
ws.close(1000, "Normal closure");
test("close() method exists and can be called", ws.readyState === 2 || ws.readyState === 3);

// Summary
console.log("");
console.log("=== WebSocket Test Results ===");
const passed = tests_passed;
const failed = tests_failed;
console.log("Tests passed: " + passed);
console.log("Tests failed: " + failed);

if (failed === 0) {
    console.log("All tests passed!");
}
