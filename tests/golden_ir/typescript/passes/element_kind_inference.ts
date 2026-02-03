// Test: Element kind inference for array literals
// RUN: %ts-aot %s -o %t.exe && %t.exe

// Element kind inference determines optimal storage strategy at compile time:
// - [1, 2, 3, 4, 5] -> PackedSmi (small integers) - inferred but uses generic path for compat
// - [1.5, 2.5, 3.0] -> PackedDouble (floats) - inferred but uses generic path for compat
// - ["a", "b", "c"] -> PackedString (strings) - inferred but uses generic path for compat
// - [1, "hello", true] -> PackedAny (mixed types via TupleType)
//
// NOTE: The element kind IS inferred and stored in the ArrayType, but specialized
// codegen is not yet used for inferred kinds because runtime push/pop expects boxed values.
// This test verifies the arrays work correctly with the generic path while kind is tracked.

// OUTPUT: 15
// OUTPUT: abc
// OUTPUT: 9
// OUTPUT: 3

function user_main(): number {
    // Integer array - should be PackedSmi
    const ints = [1, 2, 3, 4, 5];
    let sum = 0;
    for (let i = 0; i < ints.length; i++) {
        sum += ints[i];
    }
    console.log(sum);

    // String array - should be PackedString
    const strs = ["a", "b", "c"];
    console.log(strs.join(""));

    // Float array - should be PackedDouble
    const floats = [1.5, 2.5, 3.0, 2.0];
    let fsum = 0;
    for (let i = 0; i < floats.length; i++) {
        fsum += floats[i];
    }
    console.log(fsum);

    // Mixed array - should be PackedAny (via TupleType)
    const mixed = [1, "hello", true];
    console.log(mixed.length);

    return 0;
}
