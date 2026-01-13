// Test enum reverse mapping

enum Color {
    Red = 0,
    Green = 1,
    Blue = 2
}

enum Direction {
    Up = 10,
    Down = 20,
    Left = 30,
    Right = 40
}

function user_main(): number {
    // Expected output:
    // 0
    // 1
    // Red
    // Green
    // Blue
    // Up
    // Right
    // Green
    // undefined

    // Forward access
    console.log(Color.Red);
    console.log(Color.Green);

    // Reverse mapping
    console.log(Color[0]);
    console.log(Color[1]);
    console.log(Color[2]);

    // Non-sequential
    console.log(Direction[10]);
    console.log(Direction[40]);

    // Dynamic
    const val = Color.Green;
    console.log(Color[val]);

    // Invalid
    console.log(Color[99]);

    return 0;
}
