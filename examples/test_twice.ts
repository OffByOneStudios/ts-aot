// Test: calling the same function twice
function keys(obj: any): string[] {
    console.log("keys: entering");
    const result: string[] = [];
    for (const key in obj) {
        console.log("  key: " + key);
        result.push(key);
    }
    console.log("keys: returning " + result.length);
    return result;
}

const testObj = { a: 1, b: 2, c: 3 };

console.log("First call:");
const r1 = keys(testObj);
console.log("Got " + r1.length + " keys");

console.log("Second call:");
const r2 = keys(testObj);
console.log("Got " + r2.length + " keys");
