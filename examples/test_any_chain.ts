// Test: for-in on any vs static object
function keysOfAny(obj: any): string[] {
    const result: string[] = [];
    for (const key in obj) {
        result.push(key);
    }
    return result;
}

function wrapper(x: any): string[] {
    console.log("wrapper calling keysOfAny");
    return keysOfAny(x);
}

const testObj = { a: 1, b: 2, c: 3 };

console.log("Direct call:");
const r1 = keysOfAny(testObj);
console.log("Got " + r1.length + " keys");

console.log("Through wrapper:");
const r2 = wrapper(testObj);
console.log("Got " + r2.length + " keys");
