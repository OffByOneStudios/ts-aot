// Debug for-in key retrieval

const source: any = {
    fn1: function() { return 1; },
    fn2: function() { return 2; }
};

console.log("Getting keys");
for (const key in source) {
    console.log("key length: " + key.length);
    console.log("key: '" + key + "'");
}
console.log("Done");
