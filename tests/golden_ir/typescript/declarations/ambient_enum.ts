// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Testing ambient enums...
// OUTPUT: Red: 0
// OUTPUT: Green: 1
// OUTPUT: Blue: 2
// OUTPUT: Status OK: 200
// OUTPUT: Status Not Found: 404
// OUTPUT: All tests passed!

// Test: Ambient enum declarations (declare enum)

// Ambient enum with explicit values
declare enum Color {
    Red = 0,
    Green = 1,
    Blue = 2
}

// Ambient enum with auto-increment
declare enum Direction {
    Up,
    Down,
    Left,
    Right
}

// Ambient enum with mixed values
declare enum StatusCode {
    OK = 200,
    NotFound = 404,
    ServerError = 500
}

function user_main(): number {
    console.log("Testing ambient enums...");

    // Test 1: Basic ambient enum access
    console.log("Red: " + Color.Red);
    console.log("Green: " + Color.Green);
    console.log("Blue: " + Color.Blue);

    // Test 2: Ambient enum with explicit HTTP status codes
    console.log("Status OK: " + StatusCode.OK);
    console.log("Status Not Found: " + StatusCode.NotFound);

    console.log("All tests passed!");
    return 0;
}
