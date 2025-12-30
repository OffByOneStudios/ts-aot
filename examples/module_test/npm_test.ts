// Test importing from node_modules
import { square, cube, factorial, circleArea, PI } from "my-math-lib";

console.log("=== Testing node_modules import ===");

console.log("square(5):");
console.log(square(5));

console.log("cube(3):");
console.log(cube(3));

console.log("factorial(5):");
console.log(factorial(5));

console.log("PI:");
console.log(PI);

console.log("circleArea(2):");
console.log(circleArea(2));

console.log("=== node_modules test complete ===");
