// Debug why mixin isn't finding methods

const obj = {
    a: function() { return 1; },
    b: function() { return 2; }
};

console.log("Test 1: Direct typeof check");
console.log("typeof obj.a = " + typeof obj.a);
console.log("typeof obj.b = " + typeof obj.b);

console.log("\nTest 2: Keys");
const result: string[] = [];
for (const key in obj) {
    console.log("Found key: " + key);
    result.push(key);
}
console.log("Keys found: " + result.length);

console.log("\nTest 3: Computed access");
for (let i = 0; i < result.length; i++) {
    const key = result[i];
    const value = obj[key];
    console.log("obj[" + key + "] = " + value);
    console.log("typeof obj[" + key + "] = " + typeof value);
}

console.log("\nDone");
