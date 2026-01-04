// Test long-running program with GC enabled
// Creates many objects to trigger garbage collection

console.log("GC stress test starting...");

function createObjects(count: number): number {
    let total = 0;
    for (let i = 0; i < count; i++) {
        // Create temporary objects that should be collected
        const obj = { x: i, y: i * 2, z: i * 3 };
        total = total + obj.x + obj.y + obj.z;
    }
    return total;
}

// Run multiple iterations to stress GC
for (let round = 0; round < 5; round++) {
    const result = createObjects(10000);
    console.log("Round " + (round + 1) + ": sum = " + result);
}

console.log("GC stress test complete - no crashes!");
