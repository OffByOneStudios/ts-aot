// Test isEqual function only - with null handling

function isEqual(a: any, b: any): boolean {
    // Same reference or both same primitive value
    if (a === b) {
        return true;
    }
    
    // Handle null/undefined - comparing primitives to null is OK now
    if (a === null || a === undefined || b === null || b === undefined) {
        return a === b;
    }
    
    return false;
}

console.log("isEqual(1, 1) =", isEqual(1, 1));
console.log("isEqual(1, 2) =", isEqual(1, 2));
console.log("isEqual('a', 'a') =", isEqual("a", "a"));
console.log("isEqual('a', 'b') =", isEqual("a", "b"));
