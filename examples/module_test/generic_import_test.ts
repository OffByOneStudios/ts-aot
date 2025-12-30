// Test importing and using generic functions from another module

import { identity, first, map } from "./generic_utils";

// Test identity with different types
const n = identity(42);
console.log("Identity number:");
console.log(n);

const s = identity("hello");
console.log("Identity string:");
console.log(s);

// Test first with arrays
const nums = [1, 2, 3, 4, 5];
const firstNum = first(nums);
console.log("First number:");
console.log(firstNum);

const strs = ["a", "b", "c"];
const firstStr = first(strs);
console.log("First string:");
console.log(firstStr);

// Test map with transformation
const doubled = map([1, 2, 3], (x: number) => x * 2);
console.log("Doubled first:");
console.log(doubled[0]);
console.log("Doubled second:");
console.log(doubled[1]);

console.log("Generic import test complete!");
