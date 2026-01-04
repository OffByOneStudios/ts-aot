// Test if iterating over object properties causes memory leak

console.log("Test 1: Simple object iteration");

const obj = {
    a: 1,
    b: 2,
    c: 3,
    d: 4,
    e: 5
};

console.log("Object created");

let count = 0;
for (const key in obj) {
    count++;
    console.log("Key: " + key + " = " + obj[key]);
}

console.log("Found " + count + " keys");

console.log("Test 2: Function properties");

const methods = {
    add: function(a: number, b: number): number { return a + b; },
    sub: function(a: number, b: number): number { return a - b; },
    mul: function(a: number, b: number): number { return a * b; }
};

console.log("Methods object created");

count = 0;
for (const key in methods) {
    count++;
    const value = methods[key];
    console.log("Method: " + key + " type=" + typeof value);
}

console.log("Found " + count + " methods");

console.log("Test 3: Copy properties");

const target: any = {};
for (const key in methods) {
    target[key] = methods[key];
}

console.log("Copied " + count + " methods");
console.log("target.add exists: " + (typeof target.add === 'function'));

const result = target.add(5, 3);
console.log("target.add(5, 3) = " + result);

console.log("Done");
