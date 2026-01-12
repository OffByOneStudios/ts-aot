// Test RegExp sticky flag (y) with exec

function user_main(): number {
    console.log("Sticky flag test with exec:");

    const str = "aaa_aa_a";
    const regex = /a+/y;

    // First match at position 0
    let match = regex.exec(str);
    if (match) {
        console.log(match[0]);  // "aaa"
        console.log(regex.lastIndex);  // 3
    }

    // Second match must start exactly at position 3
    // Position 3 is "_" so should fail
    match = regex.exec(str);
    if (match) {
        console.log("match found: " + match[0]);
    } else {
        console.log("no match");  // Expected
    }

    // After failure, lastIndex should reset to 0
    console.log(regex.lastIndex);  // 0

    return 0;
}
