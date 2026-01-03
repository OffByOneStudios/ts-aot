// Test calling Object() constructor
console.log("Testing Object constructor...");

const obj1 = Object();
console.log("Object() returned:", obj1);

const obj2 = Object(42);
console.log("Object(42) returned:", obj2);

const obj3 = Object({foo: "bar"});
console.log("Object({foo:'bar'}) returned:", obj3);

console.log("All Object() tests passed!");
