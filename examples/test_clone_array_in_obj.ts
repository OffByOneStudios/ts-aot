// Test cloning an object with an array property

function cloneDeep<T>(value: T): T {
    if (value === null) return value;
    
    const t = typeof value;
    if (t === "number" || t === "string" || t === "boolean") return value;
    
    if (Array.isArray(value)) {
        console.log("  Cloning array of length:", (value as any[]).length);
        const arr = value as any[];
        const result: any[] = [];
        for (let i = 0; i < arr.length; i = i + 1) {
            result.push(cloneDeep(arr[i]));
        }
        console.log("  Cloned array length:", result.length);
        return result as T;
    }
    
    console.log("  Cloning object...");
    const obj = value as any;
    const result: any = {};
    const keys = Object.keys(obj);
    for (let i = 0; i < keys.length; i = i + 1) {
        const key = keys[i];
        console.log("  Setting key:", key);
        result[key] = cloneDeep(obj[key]);
    }
    return result as T;
}

console.log("Test: Clone object with array");
const original = { nums: [1, 2, 3] };
console.log("Original nums[0]:", original.nums[0]);
console.log("Calling cloneDeep...");
const cloned = cloneDeep(original);
console.log("cloneDeep returned");
console.log("About to access cloned.nums...");
const nums = cloned.nums;
console.log("Got cloned.nums");
console.log("Type check - Array.isArray(nums):", Array.isArray(nums));
console.log("cloned.nums.length:", nums.length);
if (nums.length > 0) {
    console.log("cloned.nums[0]:", nums[0]);
} else {
    console.log("Array is empty!");
}
