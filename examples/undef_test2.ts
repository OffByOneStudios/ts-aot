// More detailed undefined test
console.log("Test 1: Direct undefined assignment");
var x = undefined;
console.log("typeof x:", typeof x);

console.log("\nTest 2: Property access returning undefined");
var obj = { a: 1 };
var y = obj.b;
console.log("typeof y:", typeof y);

console.log("\nTest 3: Comparison with undefined literal");
if (x === undefined) {
    console.log("x === undefined: TRUE");
} else {
    console.log("x === undefined: FALSE");
}

if (y === undefined) {
    console.log("y === undefined: TRUE");
} else {
    console.log("y === undefined: FALSE");
}

console.log("\nTest 4: Check if x is null");
if (x === null) {
    console.log("x === null: TRUE");
} else {
    console.log("x === null: FALSE");
}

console.log("done");
