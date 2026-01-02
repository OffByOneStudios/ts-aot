// Test Array.isArray

const arr = [1, 2, 3];
const obj = { x: 1 };
const num = 42;
const str = "hello";

console.log("Testing Array.isArray:");
console.log("Array.isArray([1,2,3]):", Array.isArray(arr));
console.log("Array.isArray({x:1}):", Array.isArray(obj));
console.log("Array.isArray(42):", Array.isArray(num));
console.log("Array.isArray('hello'):", Array.isArray(str));

if (Array.isArray(arr) && !Array.isArray(obj) && !Array.isArray(num) && !Array.isArray(str)) {
    console.log("PASS: Array.isArray works correctly");
} else {
    console.log("FAIL");
}
