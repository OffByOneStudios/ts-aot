// Test object literal with functions

const obj = {
    fn1: function() { return 1; },
    fn2: function() { return 2; }
};

console.log("Step 1: Getting keys");
const keys: string[] = [];
for (const key in obj) {
    console.log("Found key: " + key);
    keys.push(key);
}
console.log("Step 2: Got " + keys.length + " keys");

console.log("Step 3: Checking typeof");
for (let i = 0; i < keys.length; i++) {
    const k = keys[i];
    console.log("Key: " + k);
    const v = (obj as any)[k];
    console.log("typeof: " + typeof v);
}
console.log("Done");
