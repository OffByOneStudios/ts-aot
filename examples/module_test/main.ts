// Test: Local module imports
// This file imports from another local TypeScript file

import { add, multiply } from "./math_utils";
import { greet } from "./string_utils";

// Test imported functions directly
console.log("Testing add(5, 3):", add(5, 3));
console.log("Testing multiply(4, 7):", multiply(4, 7));
console.log("Testing greet:", greet("World"));

// Test default export
import Calculator from "./calculator";

const calc = new Calculator(10);
console.log("Calc add 5:", calc.add(5));
console.log("Calc result:", calc.getResult());
