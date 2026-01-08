// Test String.prototype.padStart() and padEnd()

function user_main(): number {
    // Test padStart
    const str1 = "5";
    console.log("padStart: '" + str1.padStart(3, "0") + "'");  // "005"

    // Test padEnd
    const str2 = "5";
    console.log("padEnd: '" + str2.padEnd(3, "0") + "'");  // "500"

    return 0;
}
