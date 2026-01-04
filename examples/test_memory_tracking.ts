// Test memory tracking
declare function __ts_memory_tracking_enable(): void;
declare function __ts_memory_tracking_report(): void;

console.log("Enabling memory tracking...");
__ts_memory_tracking_enable();

console.log("Creating objects...");
for (let i = 0; i < 1000; i++) {
    const obj: any = { x: i, y: i * 2, name: "test" + i };
    const arr = [1, 2, 3, 4, 5];
    const str = "Hello " + i;
}

console.log("\nMemory report:");
__ts_memory_tracking_report();
