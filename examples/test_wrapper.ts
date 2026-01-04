// Test: calling through wrapper after direct call
function keys(obj: any): string[] {
    console.log("keys: entering");
    const result: string[] = [];
    for (const key in obj) {
        console.log("  key");
        result.push(key);
    }
    console.log("keys: returning " + result.length);
    return result;
}

function wrapper(source: any): string[] {
    console.log("wrapper: calling keys");
    return keys(source);
}

const testObj = { a: 1, b: 2, c: 3 };

console.log("Direct call:");
const r1 = keys(testObj);
console.log("Got " + r1.length + " keys");

console.log("Through wrapper:");
const r2 = wrapper(testObj);
console.log("Got " + r2.length + " keys");
