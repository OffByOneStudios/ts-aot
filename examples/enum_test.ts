// Basic enum test

enum Color {
    Red,
    Green,
    Blue
}

enum Status {
    Active = 1,
    Inactive = 2,
    Pending = 10
}

// Test enum access
const red = Color.Red;
const green = Color.Green;
const blue = Color.Blue;

console.log("Color.Red =", red);      // Should print 0
console.log("Color.Green =", green);  // Should print 1
console.log("Color.Blue =", blue);    // Should print 2

// Test enum with explicit values
const active = Status.Active;
const inactive = Status.Inactive;
const pending = Status.Pending;

console.log("Status.Active =", active);    // Should print 1
console.log("Status.Inactive =", inactive); // Should print 2
console.log("Status.Pending =", pending);   // Should print 10

// Test enum in comparison
if (red === Color.Red) {
    console.log("red equals Color.Red");
}

// Test enum as function parameter
function printColor(c: Color): void {
    if (c === Color.Red) {
        console.log("It's red!");
    } else if (c === Color.Green) {
        console.log("It's green!");
    } else {
        console.log("It's blue!");
    }
}

printColor(Color.Red);
printColor(Color.Green);
printColor(Color.Blue);

console.log("All enum tests passed!");
