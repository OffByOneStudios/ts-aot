// Simple cloneDeep test for objects

function cloneDeep<T>(value: T): T {
    // Handle null/undefined
    if (value === null) {
        return value;
    }
    
    // Handle primitives
    const t = typeof value;
    if (t === "number" || t === "string" || t === "boolean") {
        return value;
    }
    
    // Handle arrays
    if (Array.isArray(value)) {
        const arr = value as any[];
        const result: any[] = [];
        for (let i = 0; i < arr.length; i = i + 1) {
            result.push(cloneDeep(arr[i]));
        }
        return result as T;
    }
    
    // Handle objects
    console.log("  Cloning object...");
    const obj = value as any;
    const result: any = {};
    console.log("  Empty result created");
    const keys = Object.keys(obj);
    console.log("  Keys:", keys.length);
    for (let i = 0; i < keys.length; i = i + 1) {
        const key = keys[i];
        console.log("  Processing key:", key);
        const propValue = obj[key];
        console.log("  Property value:", propValue);
        const clonedValue = cloneDeep(propValue);
        console.log("  Cloned value:", clonedValue);
        result[key] = clonedValue;
        console.log("  Set on result done");
        // Verify it was set
        const verify = result[key];
        console.log("  Verification read:", verify);
    }
    console.log("  About to return result");
    return result as T;
}

console.log("Test 1: Clone simple object");
const obj = { x: 1, y: 2 };
console.log("Original:", obj.x, obj.y);
const cloned = cloneDeep(obj);
console.log("Cloned object returned");
console.log("About to access cloned.x");
const xval = cloned.x;
console.log("Got xval:", xval);
