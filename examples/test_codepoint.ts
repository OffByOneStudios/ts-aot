// Test String.fromCodePoint() and String.prototype.codePointAt()

function user_main(): number {
    // Test 1: Basic ASCII characters
    console.log("Test 1 - ASCII:");
    const s1 = String.fromCodePoint(65, 66, 67);  // ABC
    console.log(s1);

    // Test 2: Unicode characters
    console.log("Test 2 - Unicode:");
    const s2 = String.fromCodePoint(0x1F600);  // Grinning face emoji
    console.log(s2);

    // Test 3: Mixed
    console.log("Test 3 - Mixed:");
    const s3 = String.fromCodePoint(72, 105, 33);  // Hi!
    console.log(s3);

    // Test 4: codePointAt
    console.log("Test 4 - codePointAt:");
    const hello = "Hello";
    console.log(hello.codePointAt(0));  // 72 (H)
    console.log(hello.codePointAt(1));  // 101 (e)
    console.log(hello.codePointAt(4));  // 111 (o)

    // Test 5: codePointAt on emoji (surrogate pair)
    console.log("Test 5 - Emoji codePointAt:");
    const emoji = String.fromCodePoint(0x1F600);
    const cp = emoji.codePointAt(0);
    console.log(cp);  // Should print 128512 (0x1F600)

    return 0;
}
