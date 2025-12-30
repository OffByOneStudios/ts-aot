// Test importing and using a class from another module

import { Person } from "./person";

// Create an instance of the imported class
const p = new Person("Alice", 30);

// Test property access
console.log("Name:");
console.log(p.name);

// Test method call
console.log("Greeting:");
console.log(p.greet());

// Test another method
console.log("Age:");
console.log(p.getAge());

console.log("Class import test complete!");
