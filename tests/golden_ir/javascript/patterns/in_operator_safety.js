// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: true
// OUTPUT: false
// OUTPUT: true
// OUTPUT: false

// Normal object property check
var obj = { a: 1, b: 2 };
console.log("a" in obj);
console.log("c" in obj);

// Array - check numeric index as string key
var arr = [10, 20, 30];
console.log("length" in arr);

// Non-object RHS should not crash (JS throws TypeError, we return false)
var str = "hello";
var result = "length" in str;
console.log(result);
