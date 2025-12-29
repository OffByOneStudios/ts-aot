// Test for remaining process milestones (102.8, 102.9, 102.11, 102.12, 102.13, 102.14)

console.log("=== Testing Process Extended APIs (Part 2) ===\n");

// ============================================================================
// Milestone 102.8: Process Events
// ============================================================================
console.log("--- Milestone 102.8: Process Events ---");

// Test process.on() - register an exit handler
let exitHandlerCalled = false;
process.on('exit', (code: number) => {
    console.log("Exit handler called with code:", code);
    exitHandlerCalled = true;
});

// Test process.hasUncaughtExceptionCaptureCallback()
const hasCapture = process.hasUncaughtExceptionCaptureCallback();
console.log("hasUncaughtExceptionCaptureCallback:", hasCapture);

// ============================================================================
// Milestone 102.9: Event Loop Handles
// ============================================================================
console.log("\n--- Milestone 102.9: Event Loop Handles ---");

// Test process.getActiveResourcesInfo()
const resources = process.getActiveResourcesInfo();
console.log("Active resources count:", resources.length);
if (resources.length > 0) {
    console.log("First resource type:", resources[0]);
}

// Test process.ref() and process.unref()
console.log("Calling process.ref()...");
process.ref();
console.log("Calling process.unref()...");
process.unref();

// ============================================================================
// Milestone 102.11: TextEncoder/TextDecoder
// ============================================================================
console.log("\n--- Milestone 102.11: TextEncoder/TextDecoder ---");

// Test TextEncoder
const encoder = new TextEncoder();
console.log("TextEncoder encoding:", encoder.encoding);

const encoded = encoder.encode("Hello, World!");
console.log("Encoded length:", encoded.length);
console.log("Encoded bytes exist:", encoded.length > 0);

// Test TextDecoder
const decoder = new TextDecoder();
console.log("TextDecoder encoding:", decoder.encoding);
console.log("TextDecoder fatal:", decoder.fatal);
console.log("TextDecoder ignoreBOM:", decoder.ignoreBOM);

// Test round-trip encoding/decoding
const testStr = "Hello, ts-aot!";
const encodedBytes = encoder.encode(testStr);
const decodedStr = decoder.decode(encodedBytes);
console.log("Original:", testStr);
console.log("Round-trip:", decodedStr);
console.log("Round-trip matches:", testStr === decodedStr);

// ============================================================================
// Milestone 102.12: Memory Info
// ============================================================================
console.log("\n--- Milestone 102.12: Memory Info ---");

// Test process.constrainedMemory()
const constrained = process.constrainedMemory();
console.log("constrainedMemory:", constrained);

// Test process.availableMemory()
const available = process.availableMemory();
console.log("availableMemory > 0:", available > 0);

// ============================================================================
// Milestone 102.13: Internal/Debug APIs
// ============================================================================
console.log("\n--- Milestone 102.13: Internal/Debug APIs ---");

// Test process._getActiveHandles()
const handles = process._getActiveHandles();
console.log("_getActiveHandles():", Array.isArray(handles));

// Test process._getActiveRequests()
const requests = process._getActiveRequests();
console.log("_getActiveRequests():", Array.isArray(requests));

// Test process._tickCallback()
console.log("Calling _tickCallback()...");
process._tickCallback();
console.log("_tickCallback() completed");

// ============================================================================
// Milestone 102.14: Diagnostics & Reporting
// ============================================================================
console.log("\n--- Milestone 102.14: Diagnostics & Reporting ---");

// Test process.debugPort (already tested in previous test)
console.log("debugPort:", process.debugPort);

// Test process.report
const report = process.report;
console.log("process.report exists:", report !== undefined);

console.log("\n=== All Extended Process API Tests Complete ===");
