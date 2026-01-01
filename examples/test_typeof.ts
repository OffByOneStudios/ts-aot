// Test typeof operator

console.log("=== typeof Operator Test ===");

// Test with statically typed values
let numVal = 42;
console.log("typeof 42:");
console.log(typeof numVal);

let strVal = "hello";
console.log("typeof 'hello':");
console.log(typeof strVal);

let boolVal = true;
console.log("typeof true:");
console.log(typeof boolVal);

// Test with any type (uses runtime ts_typeof)
let anyNum: any = 123;
console.log("typeof (any)123:");
console.log(typeof anyNum);

let anyStr: any = "world";
console.log("typeof (any)'world':");
console.log(typeof anyStr);

let anyObj: any = { a: 1 };
console.log("typeof (any){a:1}:");
console.log(typeof anyObj);

let anyArr: any = [1, 2, 3];
console.log("typeof (any)[1,2,3]:");
console.log(typeof anyArr);

let anyUndef: any = undefined;
console.log("typeof (any)undefined:");
console.log(typeof anyUndef);

console.log("");
console.log("=== typeof Test Complete ===");
