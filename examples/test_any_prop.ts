// Test property access on any-typed object
const obj: any = {
    fn1: function() { return 1; },
    fn2: function() { return 2; }
};

// Get keys
const result: string[] = [];
for (const key in obj) {
    console.log("key: '" + key + "'");
    const val = obj[key];
    console.log("val type: " + typeof val);
    result.push(key);
}
console.log("Found " + result.length + " keys");
