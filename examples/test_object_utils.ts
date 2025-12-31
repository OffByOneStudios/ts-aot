// Test Object Utilities for ts-lodash

// ============ get - Deep property access ============
function get<T>(obj: any, path: string, defaultValue?: T): T | undefined {
    const keys = path.split(".");
    let result: any = obj;
    for (let i = 0; i < keys.length; i = i + 1) {
        const key = keys[i];
        if (result === null || result === undefined) {
            return defaultValue;
        }
        // Use Object.hasOwn to check if property exists
        if (!Object.hasOwn(result, key)) {
            return defaultValue;
        }
        result = (result as any)[key];
    }
    return result as T;
}

// ============ has - Check if path exists ============
function has(obj: any, path: string): boolean {
    const keys = path.split(".");
    let current: any = obj;
    for (let i = 0; i < keys.length; i = i + 1) {
        const key = keys[i];
        if (current === null || current === undefined) {
            return false;
        }
        if (!Object.hasOwn(current, key)) {
            return false;
        }
        current = (current as any)[key];
    }
    return true;
}

// ============ pick - Pick specific keys ============
function pick<T extends object>(obj: T, keys: string[]): Partial<T> {
    const result: any = {};
    for (let i = 0; i < keys.length; i = i + 1) {
        const key = keys[i];
        if (Object.hasOwn(obj, key)) {
            result[key] = (obj as any)[key];
        }
    }
    return result as Partial<T>;
}

// ============ omit - Omit specific keys ============
function omit<T extends object>(obj: T, keysToOmit: string[]): Partial<T> {
    const result: any = {};
    const allKeys = Object.keys(obj);
    for (let i = 0; i < allKeys.length; i = i + 1) {
        const key = allKeys[i];
        // Check if key is NOT in keysToOmit
        let shouldOmit = false;
        for (let j = 0; j < keysToOmit.length; j = j + 1) {
            if (keysToOmit[j] === key) {
                shouldOmit = true;
            }
        }
        if (!shouldOmit) {
            result[key] = (obj as any)[key];
        }
    }
    return result as Partial<T>;
}

// ============ clone - Shallow clone ============
function clone(obj: any): any {
    const result: any = {};
    Object.assign(result, obj);
    return result;
}

// ============ isEmpty - Check if empty ============
function isEmpty(value: any): boolean {
    if (value === null || value === undefined) {
        return true;
    }
    const keys = Object.keys(value);
    return keys.length === 0;
}

// ============ Tests ============
console.log("=== Object Utilities Test ===\n");

// Test get
console.log("1. get() - Deep property access:");
const nested = { user: { profile: { name: "Alice" } } };
const name = get(nested, "user.profile.name");
console.log("  get(nested, 'user.profile.name') =", name);
const missing = get(nested, "user.profile.age", 0);
console.log("  get(nested, 'user.profile.age', 0) =", missing);

// Test has
console.log("\n2. has() - Check path exists:");
console.log("  has(nested, 'user.profile.name') =", has(nested, "user.profile.name"));
console.log("  has(nested, 'user.profile.age') =", has(nested, "user.profile.age"));

// Test pick
console.log("\n3. pick() - Pick specific keys:");
const person = { name: "Bob", age: 30, city: "NYC" };
const picked = pick(person, ["name", "age"]);
console.log("  pick(person, ['name', 'age']):");
console.log("    keys:", Object.keys(picked));

// Test omit
console.log("\n4. omit() - Omit specific keys:");
const omitted = omit(person, ["age"]);
console.log("  omit(person, ['age']):");
console.log("    keys:", Object.keys(omitted));

// Test clone
console.log("\n5. clone() - Shallow clone:");
const cloned = clone(person);
console.log("  Original name:", person.name);
console.log("  Cloned name:", cloned.name);

// Test isEmpty
console.log("\n6. isEmpty() - Check if empty:");
const emptyObj = {};
const nonEmptyObj = { a: 1 };
console.log("  isEmpty({}) =", isEmpty(emptyObj));
console.log("  isEmpty({a: 1}) =", isEmpty(nonEmptyObj));

console.log("\n=== All tests complete ===");
