console.log("=== typeof tests ===");

let undef: any = undefined;
console.log("typeof undefined: " + typeof undef);

let numInt: any = 42;
console.log("typeof int: " + typeof numInt);

let numDbl: any = 3.14;
console.log("typeof double: " + typeof numDbl);

let bool: any = true;
console.log("typeof boolean: " + typeof bool);

let str: any = "hello";
console.log("typeof string: " + typeof str);

let obj: any = { foo: "bar" };
console.log("typeof object: " + typeof obj);

let arr: any = [1, 2, 3];
console.log("typeof array: " + typeof arr);

function myFunc() { return 1; }
let fn: any = myFunc;
console.log("typeof function: " + typeof fn);

console.log("=== done ===");
