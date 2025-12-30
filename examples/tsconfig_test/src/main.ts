// Test tsconfig.json path aliases
// Import using path aliases defined in tsconfig.json
import { add, multiply, square } from "@lib/math";
import { greet } from "@utils";

console.log("=== Testing tsconfig.json path aliases ===");

console.log("add(10, 5):");
console.log(add(10, 5));

console.log("multiply(6, 7):");
console.log(multiply(6, 7));

console.log("square(8):");
console.log(square(8));

console.log("greet('TypeScript'):");
console.log(greet("TypeScript"));

console.log("=== Path alias test complete ===");
