// Test String.prototype.repeat()

function user_main(): number {
    console.log("'ab'.repeat(3): " + "ab".repeat(3));  // "ababab"
    console.log("'x'.repeat(5): " + "x".repeat(5));    // "xxxxx"
    console.log("'hi'.repeat(0): '" + "hi".repeat(0) + "'");  // ""
    return 0;
}
