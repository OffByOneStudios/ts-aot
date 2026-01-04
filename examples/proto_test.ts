// Test Function.prototype.toString
console.log("Function:", typeof Function);
console.log("Function.prototype:", Function.prototype);
if (Function.prototype) {
    console.log("Function.prototype.toString:", Function.prototype.toString);
    if (Function.prototype.toString) {
        console.log("Has toString!");
    } else {
        console.log("toString is undefined!");
    }
}

function foo() { return 1; }
console.log("foo.toString:", foo.toString);
console.log("done");
