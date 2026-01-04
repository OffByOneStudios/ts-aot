// Test cell boxing for any-typed parameters
function test(a: any, b: any): any {
    const useB = function() {
        return b["key"];  // capture b
    };
    return a;  // a is used but not captured
}

const obj1: any = { key: 1 };
const obj2 = { key: 2 };  // Object type, not any

console.log("Before call");
const result = test(obj1, obj2);
console.log("After call: " + result);
