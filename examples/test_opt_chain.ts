// Test optional chaining
function user_main(): number {
    // Test 1: Simple optional property access on non-null
    const obj1: { a?: number } = { a: 42 };
    const val1 = obj1?.a;
    console.log(val1);  // Expected: 42

    // Test 2: Optional access on undefined property
    const obj2: { a?: number } = {};
    const val2 = obj2?.a;
    console.log(val2);  // Expected: undefined

    // Test 3: Nested optional access - the pattern from golden test
    const obj3 = { nested: { value: 100 } };
    const val3 = obj3.nested?.value;
    console.log(val3);  // Expected: 100

    // Test 4: Nested optional access where nested is undefined
    const obj4: { nested?: { value: number } } = { nested: undefined };
    const val4 = obj4.nested?.value;
    console.log(val4);  // Expected: undefined

    return 0;
}
