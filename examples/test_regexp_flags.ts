// Test RegExp flags

function user_main(): number {
    // Test dotAll flag (s) - ES2018
    const text = "line1\nline2";
    const dotAllRegex = /line1.line2/s;
    const result = dotAllRegex.test(text);
    console.log("dotAll test:");
    console.log(result);  // Should be true (. matches newline with s flag)

    // Test without dotAll - should be false
    const noDotAll = /line1.line2/;
    const result2 = noDotAll.test(text);
    console.log(result2);  // Should be false (. doesn't match newline)

    return 0;
}
