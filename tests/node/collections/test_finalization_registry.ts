// Test FinalizationRegistry ES2021

// Test 1: FinalizationRegistry creation
const registry = new FinalizationRegistry((heldValue: any) => {
    // Callback may or may not be called with Boehm GC
});
console.log("PASS: FinalizationRegistry created");

// Test 2: register does not crash
const target1 = { data: "target1" };
registry.register(target1, "held1", target1);
console.log("PASS: register() with all args works");

// Test 3: register with only 2 args
const target2 = { data: "target2" };
registry.register(target2, "held2");
console.log("PASS: register() with 2 args works");

// Test 4: unregister returns boolean
const target3 = { data: "target3" };
registry.register(target3, "held3", target3);
const result = registry.unregister(target3);
if (typeof result === "boolean") {
    console.log("PASS: unregister() returns boolean");
} else {
    console.log("FAIL: unregister() did not return boolean");
}

// Test 5: Multiple registrations
const registry2 = new FinalizationRegistry((held: any) => {});
for (let i = 0; i < 5; i++) {
    const t = { index: i };
    registry2.register(t, i);
}
console.log("PASS: Multiple registrations work");

console.log("FinalizationRegistry tests complete");

function user_main(): number {
    return 0;
}
