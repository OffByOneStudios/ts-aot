// Test typeof for function values

function myFunc(): number {
    console.log("myFunc called!");
    return 42;
}

console.log("1. typeof myFunc: " + typeof myFunc);

const obj: any = {};
obj["fn"] = myFunc;

console.log("2. Retrieving obj.fn...");
const retrieved = obj["fn"];
console.log("3. typeof retrieved: " + typeof retrieved);

if (typeof retrieved === "function") {
    console.log("4. It's a function!");
} else {
    console.log("4. Not a function: " + typeof retrieved);
}

console.log("Done");
