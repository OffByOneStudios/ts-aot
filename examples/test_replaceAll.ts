// Test String.prototype.replaceAll()

function user_main(): number {
    const str = "hello world world";

    // Test 1: Basic replaceAll
    const result1 = str.replaceAll("world", "there");
    console.log("replaceAll: " + result1);  // "hello there there"

    // Test 2: Replace single char
    const str2 = "a-b-c-d";
    const result2 = str2.replaceAll("-", "_");
    console.log("replaceAll single: " + result2);  // "a_b_c_d"

    return 0;
}
