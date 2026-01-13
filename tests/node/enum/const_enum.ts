// Test const enums (TypeScript)

const enum Direction {
    Up,
    Down,
    Left,
    Right
}

const enum Color {
    Red = 1,
    Green = 2,
    Blue = 4
}

const enum StringConst {
    Yes = "YES",
    No = "NO"
}

function user_main(): number {
    console.log("=== Const Enum Test ===");

    // Test 1: Basic const enum
    console.log("Test 1: Basic const enum");
    console.log(Direction.Up);     // 0
    console.log(Direction.Down);   // 1
    console.log(Direction.Left);   // 2
    console.log(Direction.Right);  // 3

    // Test 2: Const enum with explicit values
    console.log("Test 2: Explicit values");
    console.log(Color.Red);    // 1
    console.log(Color.Green);  // 2
    console.log(Color.Blue);   // 4

    // Test 3: String const enum
    console.log("Test 3: String const enum");
    console.log(StringConst.Yes);  // YES
    console.log(StringConst.No);   // NO

    // Test 4: Use in expressions
    console.log("Test 4: In expressions");
    const flags = Color.Red | Color.Blue;
    console.log(flags);  // 5 (1 | 4)

    console.log("=== Tests Complete ===");
    return 0;
}
