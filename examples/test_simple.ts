function add(a: number, b: number): number {
    return a + b;
}
console.log(add(1, 2));

const s = "Hello, " + "World!";
console.log(s);
console.log(s.length);
console.log(s.toLowerCase());

// TYPE-CHECK: L2:C12 BinaryExpression -> double
// TYPE-CHECK: L2:C12 Identifier -> double
// TYPE-CHECK: L2:C16 Identifier -> double
// TYPE-CHECK: L4:C13 CallExpression -> double
// TYPE-CHECK: L4:C17 NumericLiteral -> int
// TYPE-CHECK: L4:C20 NumericLiteral -> int
