function testStrings() {
    const s1 = "hello";
    const s2 = "world";
    const s3 = "hello";
    const s4 = "this is a long string that should not be small"; // Large string
    
    console.log("s1 == s3: " + (s1 == s3)); // true
    console.log("s1 == s2: " + (s1 == s2)); // false
    console.log("s1.length: " + s1.length); // 5
    console.log("s4.length: " + s4.length); // 20
    console.log("s1 + s2: " + (s1 + s2)); // helloworld
    console.log("s1.charCodeAt(0): " + s1.charCodeAt(0)); // 104 ('h')
    console.log("s1.substring(1, 3): " + s1.substring(1, 3)); // el
    console.log("'  abc  '.trim(): '" + "  abc  ".trim() + "'"); // 'abc'
}

testStrings();
