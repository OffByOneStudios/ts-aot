// Test undefined comparison
var x = undefined;
console.log("x:", x);

if (x === undefined) {
    console.log("x === undefined: TRUE");
} else {
    console.log("x === undefined: FALSE");
}

if (x == undefined) {
    console.log("x == undefined: TRUE");
} else {
    console.log("x == undefined: FALSE");
}

if (x !== undefined) {
    console.log("x !== undefined: TRUE");
} else {
    console.log("x !== undefined: FALSE");
}

// Get an undefined property
var obj = { a: 1 };
var y = obj.b;
console.log("y:", y);

if (y === undefined) {
    console.log("y === undefined: TRUE");
} else {
    console.log("y === undefined: FALSE");
}

console.log("done");
