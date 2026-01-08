// Test optional chaining without type annotations
function user_main(): number {
    // Without type annotations - types are inferred
    const obj1 = { nested: { value: 42 } };
    console.log(obj1.nested?.value);

    const obj2 = { nested: undefined };
    console.log(obj2.nested?.value);

    return 0;
}
