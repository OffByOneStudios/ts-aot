// Test console grouping methods

function user_main(): number {
    console.log("=== Console Grouping Tests ===");

    // Test 1: Basic group
    console.log("Test 1: console.group with label");
    console.group("Group 1");
    console.log("Inside group 1");
    console.log("Still in group 1");
    console.groupEnd();
    console.log("Outside group 1");

    // Test 2: Nested groups
    console.log("\nTest 2: Nested groups");
    console.group("Outer");
    console.log("In outer");
    console.group("Inner");
    console.log("In inner");
    console.groupEnd();
    console.log("Back to outer");
    console.groupEnd();
    console.log("Outside all groups");

    // Test 3: groupCollapsed (behaves same as group in terminal)
    console.log("\nTest 3: console.groupCollapsed");
    console.groupCollapsed("Collapsed Group");
    console.log("This is inside collapsed group");
    console.groupEnd();

    // Test 4: group without label
    console.log("\nTest 4: group without label");
    console.group();
    console.log("Inside unlabeled group");
    console.groupEnd();

    // Test 5: Multiple groupEnd calls (should not go negative)
    console.log("\nTest 5: Extra groupEnd calls");
    console.groupEnd();  // Should be safe even with no group
    console.groupEnd();  // Should be safe
    console.log("After extra groupEnd calls");

    console.log("\nPASS: All console grouping tests completed");
    return 0;
}
