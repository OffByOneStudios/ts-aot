// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Testing discriminated unions...
// OUTPUT: Circle area: 78.5397
// OUTPUT: Square area: 16
// OUTPUT: All tests passed!

type Circle = { kind: "circle"; radius: number };
type Square = { kind: "square"; side: number };
type Shape = Circle | Square;

function assertNever(x: never): never {
    throw new Error("Unexpected value");
}

function getArea(shape: Shape): number {
    switch (shape.kind) {
        case "circle":
            return 3.14159 * shape.radius * shape.radius;
        case "square":
            return shape.side * shape.side;
        default:
            return assertNever(shape);
    }
}

function user_main(): number {
    console.log("Testing discriminated unions...");

    const circle: Shape = { kind: "circle", radius: 5 };
    const circleArea = getArea(circle);
    console.log("Circle area: " + circleArea);

    const square: Shape = { kind: "square", side: 4 };
    const squareArea = getArea(square);
    console.log("Square area: " + squareArea);

    if (circleArea > 78 && circleArea < 79 && squareArea === 16) {
        console.log("All tests passed!");
        return 0;
    }
    return 1;
}
