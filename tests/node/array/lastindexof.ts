// Test Array.lastIndexOf and String.lastIndexOf

function user_main(): number {
    // Array.lastIndexOf tests
    const arr = [1, 2, 3, 2, 1];

    console.log(arr.lastIndexOf(2));  // Expected: 3
    console.log(arr.lastIndexOf(1));  // Expected: 4
    console.log(arr.lastIndexOf(3));  // Expected: 2
    console.log(arr.lastIndexOf(99)); // Expected: -1

    // String.lastIndexOf tests
    const str = "hello world hello";

    console.log(str.lastIndexOf("hello")); // Expected: 12
    console.log(str.lastIndexOf("world")); // Expected: 6
    console.log(str.lastIndexOf("o"));     // Expected: 16
    console.log(str.lastIndexOf("xyz"));   // Expected: -1

    return 0;
}
