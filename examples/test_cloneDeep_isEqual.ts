// Test cloneDeep and isEqual - Phase 1 lodash functions

// ============ cloneDeep - Deep clone ============
function cloneDeep<T>(value: T): T {
    // Handle null/undefined
    if (value === null || value === undefined) {
        return value;
    }
    
    // Handle primitives - typeof returns "number", "string", "boolean"
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
    const obj = value as any;
    const result: any = {};
    const keys = Object.keys(obj);
    for (let i = 0; i < keys.length; i = i + 1) {
        const key = keys[i];
        result[key] = cloneDeep(obj[key]);
    }
    return result as T;
}

// ============ isEqual - Deep equality ============
function isEqual(a: any, b: any): boolean {
    // Same reference or both same primitive value
    if (a === b) {
        return true;
    }
    
    // Handle null/undefined
    if (a === null || a === undefined || b === null || b === undefined) {
        return a === b;
    }
    
    // Handle different types
    const typeA = typeof a;
    const typeB = typeof b;
    if (typeA !== typeB) {
        return false;
    }
    
    // Handle primitives (already checked equality above)
    if (typeA === "number" || typeA === "string" || typeA === "boolean") {
        return a === b;
    }
    
    // Handle arrays
    const isArrayA = Array.isArray(a);
    const isArrayB = Array.isArray(b);
    if (isArrayA !== isArrayB) {
        return false;
    }
    if (isArrayA && isArrayB) {
        const arrA = a as any[];
        const arrB = b as any[];
        if (arrA.length !== arrB.length) {
            return false;
        }
        for (let i = 0; i < arrA.length; i = i + 1) {
            if (!isEqual(arrA[i], arrB[i])) {
                return false;
            }
        }
        return true;
    }
    
    // Handle objects - only for non-array objects
    // (arrays already handled above)
    const keysA = Object.keys(a);
    const keysB = Object.keys(b);
    if (keysA.length !== keysB.length) {
        return false;
    }
    
    for (let i = 0; i < keysA.length; i = i + 1) {
        const key = keysA[i];
        if (!Object.hasOwn(b, key)) {
            return false;
        }
        // Use dynamic property access
        const valA = (a as any)[key];
        const valB = (b as any)[key];
        if (!isEqual(valA, valB)) {
            return false;
        }
    }
    return true;
}

// ============ Tests ============
console.log("=== cloneDeep and isEqual Tests ===\n");

// Test cloneDeep with nested object
console.log("1. cloneDeep() - Deep clone:");
const original = { 
    name: "Alice",
    scores: [95, 87, 92],
    meta: { active: true, level: 5 }
};
const cloned = cloneDeep(original);
console.log("  Original name:", original.name);
console.log("  Cloned name:", cloned.name);
console.log("  Original scores[0]:", original.scores[0]);
console.log("  Cloned scores[0]:", cloned.scores[0]);
console.log("  Original meta.level:", original.meta.level);
console.log("  Cloned meta.level:", cloned.meta.level);

// Verify deep clone - modify cloned shouldn't affect original
cloned.scores.push(100);
console.log("  After cloned.scores.push(100):");
console.log("  Original scores.length:", original.scores.length);
console.log("  Cloned scores.length:", cloned.scores.length);

// Test isEqual
console.log("\n2. isEqual() - Deep equality:");
console.log("  isEqual(1, 1) =", isEqual(1, 1));
console.log("  isEqual(1, 2) =", isEqual(1, 2));
console.log("  isEqual('a', 'a') =", isEqual("a", "a"));
console.log("  isEqual('a', 'b') =", isEqual("a", "b"));
console.log("  isEqual([1,2], [1,2]) =", isEqual([1,2], [1,2]));
console.log("  isEqual([1,2], [1,3]) =", isEqual([1,2], [1,3]));
console.log("  isEqual([1,2], [1,2,3]) =", isEqual([1,2], [1,2,3]));

const objA = { x: 1, y: { z: 2 } };
const objB = { x: 1, y: { z: 2 } };
const objC = { x: 1, y: { z: 3 } };
console.log("  isEqual({x:1, y:{z:2}}, {x:1, y:{z:2}}) =", isEqual(objA, objB));
console.log("  isEqual({x:1, y:{z:2}}, {x:1, y:{z:3}}) =", isEqual(objA, objC));

console.log("\n=== All tests complete ===");
