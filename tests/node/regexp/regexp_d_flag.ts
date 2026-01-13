// Test RegExp d flag (hasIndices) - ES2022

function user_main(): number {
    console.log("=== RegExp d flag (hasIndices) test ===");

    // Test 1: Basic d flag recognition
    console.log("Test 1: hasIndices property");
    const regex1 = /hello/d;
    console.log(regex1.hasIndices);  // true
    const regex2 = /hello/;
    console.log(regex2.hasIndices);  // false

    // Test 2: exec() with d flag returns indices
    console.log("Test 2: exec with d flag");
    const regex3 = /(\w+)\s(\w+)/d;
    const result = regex3.exec("Hello World");

    if (result) {
        console.log("Match found:");
        console.log(result[0]);  // "Hello World"
        console.log(result[1]);  // "Hello"
        console.log(result[2]);  // "World"

        console.log("Match index:");
        console.log(result.index);  // 0

        console.log("Indices array:");
        const indices = result.indices;
        if (indices) {
            // Full match indices
            const fullMatch = indices[0];
            if (fullMatch) {
                console.log(fullMatch[0]);  // 0 (start)
                console.log(fullMatch[1]);  // 11 (end)
            }

            // First capture group indices
            const group1 = indices[1];
            if (group1) {
                console.log(group1[0]);  // 0 (start)
                console.log(group1[1]);  // 5 (end)
            }

            // Second capture group indices
            const group2 = indices[2];
            if (group2) {
                console.log(group2[0]);  // 6 (start)
                console.log(group2[1]);  // 11 (end)
            }
        } else {
            console.log("No indices!");
        }
    } else {
        console.log("No match!");
    }

    // Test 3: exec() without d flag
    console.log("Test 3: exec without d flag");
    const regex4 = /(\w+)/;
    const result2 = regex4.exec("Hello");
    if (result2) {
        console.log(result2[0]);  // "Hello"
        // indices should be undefined
        if (result2.indices) {
            console.log("Has indices (unexpected)");
        } else {
            console.log("No indices (expected)");
        }
    }

    // Test 4: Multiple flags including d
    console.log("Test 4: Multiple flags");
    const regex5 = /test/gid;
    console.log(regex5.hasIndices);  // true
    console.log(regex5.global);      // true
    console.log(regex5.ignoreCase);  // true

    console.log("=== Tests complete ===");
    return 0;
}
