// Debug test for optional chaining - exact original test case
function user_main(): number {
    // This is the exact pattern from the failing test
    const obj1 = { nested: { value: 42 } };

    // Regular access for comparison
    console.log("Direct obj1.nested.value:");
    console.log(obj1.nested.value);  // 42 - works

    // The pattern from the test: first access is non-optional, second is optional
    console.log("obj1.nested?.value:");
    console.log(obj1.nested?.value);  // Should be 42

    return 0;
}
