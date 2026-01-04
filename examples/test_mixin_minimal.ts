// Minimal mixin test - just 2 methods

function keys(obj: any): string[] {
    console.log("keys: entering");
    const result: string[] = [];
    for (const key in obj) {
        console.log("keys: found key: " + key);
        result.push(key);
    }
    console.log("keys: returning " + result.length + " keys");
    return result;
}

function isFunction(value: any): boolean {
    const t = typeof value;
    console.log("isFunction: typeof = " + t);
    return t === 'function';
}

const source: any = {
    fn1: function() { return 1; },
    fn2: function() { return 2; }
};

console.log("Step 1: Getting keys");
const props = keys(source);
console.log("Step 2: Got " + props.length + " props");

console.log("Step 3: Checking each prop");
for (let i = 0; i < props.length; i++) {
    const key = props[i];
    console.log("Step 3." + i + ": checking key '" + key + "'");
    const val = source[key];
    console.log("Step 3." + i + ": got value, checking isFunction");
    if (isFunction(val)) {
        console.log("Step 3." + i + ": IS a function");
    } else {
        console.log("Step 3." + i + ": NOT a function");
    }
}

console.log("Done");
