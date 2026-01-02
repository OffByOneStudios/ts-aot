// Test generic function with null check

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
        console.log("  Value is array");
        const arr = value as any[];
        console.log("  Array length:", arr.length);
        const result: any[] = [];
        for (let i = 0; i < arr.length; i = i + 1) {
            console.log("  Processing index:", i);
            const elem = arr[i];
            console.log("  Element:", elem);
            const cloned = cloneDeep(elem);
            console.log("  Cloned:", cloned);
            result.push(cloned);
        }
        return result as T;
    }
    
    return value;
}

const x = cloneDeep(42);
console.log("cloneDeep(42) =", x);

console.log("\nCloning array:");
const arr = cloneDeep([1, 2, 3]);
console.log("cloneDeep([1,2,3]):", arr);



