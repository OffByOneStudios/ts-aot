/**
 * Very simple test of ts-lodash without nested loops
 */
import { identity, head, last, take, drop, tail } from './index';

// Test identity
console.log("=== Identity ===");
const num = identity(42);
console.log(num);

const str = identity("hello");
console.log(str);

// Test head/last on simple arrays
console.log("=== Head/Last ===");
const arr = [10, 20, 30, 40, 50];
console.log("head:");
console.log(head(arr));

console.log("last:");
console.log(last(arr));

// Test tail
console.log("=== Tail ===");
const tailed = tail(arr);
console.log("tail length:");
console.log(tailed.length);

// Test take
console.log("=== Take ===");
const taken = take(arr, 3);
console.log("take(3) length:");
console.log(taken.length);

// Test drop  
console.log("=== Drop ===");
const dropped = drop(arr, 2);
console.log("drop(2) length:");
console.log(dropped.length);

console.log("=== Done! ===");
