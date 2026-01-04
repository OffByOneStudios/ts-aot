// Test numeric property access performance
const obj: any = {};

// Create 1000 numeric properties
console.log("Creating 1000 numeric properties...");
for (let i = 0; i < 1000; i++) {
    obj[i] = i * 2;
}

// Access them 10000 times
console.log("Accessing properties 10000 times...");
let sum = 0;
for (let j = 0; j < 10; j++) {
    for (let i = 0; i < 1000; i++) {
        sum += obj[i];
    }
}

console.log("Sum:", sum);
console.log("Done!");
