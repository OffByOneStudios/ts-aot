// Test that functions have .prototype property
function foo() {
    return 1;
}

console.log("foo.prototype:", foo.prototype);
console.log("foo.prototype type:", typeof foo.prototype);

// Access foo.prototype.constructor
if (foo.prototype) {
    console.log("prototype exists");
} else {
    console.log("prototype is undefined");
}

// Test accessing .length on a function
console.log("foo.length:", foo.length);

// Test Array.prototype
console.log("Array.prototype:", Array.prototype);

console.log("done");
