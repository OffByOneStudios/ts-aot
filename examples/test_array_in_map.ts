// Test: clone an object that contains an array
// Simulating the cloneDeep pattern

function cloneDeep<T>(value: T): T {
    console.log("=== cloneDeep called ===");
    
    // Handle arrays
    if (Array.isArray(value)) {
        console.log("cloneDeep: value is array");
        const arr = value as any[];
        const result: any[] = [];
        for (let i = 0; i < arr.length; i = i + 1) {
            result.push(cloneDeep(arr[i]));
        }
        console.log("cloneDeep: returning array, length:", result.length);
        return result as T;
    }
    
    // Handle objects
    if (typeof value === "object" && value !== null) {
        console.log("cloneDeep: value is object");
        const result: any = {};
        const keys = Object.keys(value as object);
        console.log("cloneDeep: keys.length:", keys.length);
        for (let i = 0; i < keys.length; i = i + 1) {
            const key = keys[i];
            console.log("cloneDeep: cloning key:", key);
            const propValue = (value as any)[key];
            console.log("cloneDeep: propValue type:", typeof propValue);
            if (Array.isArray(propValue)) {
                console.log("cloneDeep: propValue is array, length:", (propValue as any[]).length);
            }
            const clonedProp = cloneDeep(propValue);
            console.log("cloneDeep: clonedProp type:", typeof clonedProp);
            if (Array.isArray(clonedProp)) {
                console.log("cloneDeep: clonedProp is array, length:", (clonedProp as any[]).length);
            }
            result[key] = clonedProp;
            
            // DEBUG: immediately retrieve it back
            const retrieved = result[key];
            console.log("cloneDeep: IMMEDIATELY after store, retrieved type:", typeof retrieved);
            if (Array.isArray(retrieved)) {
                console.log("cloneDeep: retrieved is array, length:", (retrieved as any[]).length);
            }
        }
        console.log("cloneDeep: returning object");
        return result as T;
    }
    
    // Primitives
    console.log("cloneDeep: returning primitive, type:", typeof value);
    return value;
}

// Test object with an array property
console.log("=== MAIN START ===");
const original = { scores: [1, 2, 3] };
console.log("Original scores length:", original.scores.length);

const cloned = cloneDeep(original);
console.log("=== MAIN: After cloneDeep ===");
console.log("cloned type:", typeof cloned);
const clonedScores = (cloned as any).scores;
console.log("cloned.scores type:", typeof clonedScores);
console.log("Array.isArray(cloned.scores):", Array.isArray(clonedScores));
if (Array.isArray(clonedScores)) {
    console.log("cloned.scores.length:", (clonedScores as any[]).length);
}
console.log("=== MAIN END ===");
