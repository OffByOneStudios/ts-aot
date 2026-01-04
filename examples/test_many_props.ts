// Test setting many properties on an object
console.log("Starting property test...");

const obj: any = {};

// Set 100000 properties
for (let i = 0; i < 100000; i = i + 1) {
    const key = "prop" + i;
    obj[key] = i;
}

console.log("Done setting 100000 properties");
console.log("Property count:", Object.keys(obj).length);
