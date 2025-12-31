// Test pick isolation

function pick(obj: any, keys: string[]): any {
    console.log("pick called, keys.length =", keys.length);
    const result: any = {};
    console.log("result object created");
    
    for (let i = 0; i < keys.length; i = i + 1) {
        const key = keys[i];
        console.log("  key:", key);
        if (Object.hasOwn(obj, key)) {
            console.log("    key exists in obj");
            result[key] = (obj as any)[key];
            console.log("    assigned to result");
        }
    }
    return result;
}

console.log("=== Pick Test ===");

const person = { name: "Bob", age: 30, city: "NYC" };
console.log("person created");

const keysToGet: string[] = ["name", "age"];
console.log("keysToGet created, length:", keysToGet.length);

const picked = pick(person, keysToGet);
console.log("pick complete");

console.log("picked keys:", Object.keys(picked));
