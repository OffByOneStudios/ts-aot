// Test to debug nested function calls
import { add, multiply, square } from './simple_lib';

// This works
console.log("add(2, 3): " + add(2, 3));

// Let's debug intermediate results
const m = multiply(2, 3);
console.log("multiply(2, 3) = " + m);

const s = square(2);
console.log("square(2) = " + s);

// Now pass them to add
const r1 = add(m, s);
console.log("add(m, s) = " + r1);

// What about direct nested?
const r2 = add(multiply(2, 3), square(2));
console.log("add(multiply(2, 3), square(2)) = " + r2);
