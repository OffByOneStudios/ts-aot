// Test for-in with any typed object

const source: any = {
    fn1: function() { return 1; },
    fn2: function() { return 2; }
};

console.log("Getting keys from 'any' object");
const keys: string[] = [];
for (const key in source) {
    console.log("Found key: '" + key + "'");
    keys.push(key);
}
console.log("Got " + keys.length + " keys");

console.log("Checking typeof for each key");
for (let i = 0; i < keys.length; i++) {
    const k = keys[i];
    const v = source[k];
    console.log("key=" + k + " typeof=" + typeof v);
}

console.log("Done");
