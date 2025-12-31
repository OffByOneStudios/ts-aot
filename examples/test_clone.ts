// Test clone function

const source = { name: "Bob", age: 30 };
console.log("Source created");
console.log("Source keys:", Object.keys(source));
console.log("Source name:", source.name);

const target: any = {};
console.log("\nTarget created (empty)");
console.log("Target keys before assign:", Object.keys(target));

Object.assign(target, source);
console.log("\nAfter Object.assign");
console.log("Target keys:", Object.keys(target));
console.log("Target name:", target.name);
console.log("Target age:", target.age);
