// Test passing static object to any parameter
function keys(obj: any): string[] {
    const result: string[] = [];
    console.log("keys: before for-in");
    for (const key in obj) {
        console.log("  key: '" + key + "'");
        result.push(key);
    }
    console.log("keys: after for-in, found " + result.length);
    return result;
}

const lodashMethods = {
    chunk: function(arr: any[], size: number) { return arr; },
    compact: function(arr: any[]) { return arr; }
};

console.log("Calling keys()");
const k = keys(lodashMethods);
console.log("Done: " + k.length + " keys");
