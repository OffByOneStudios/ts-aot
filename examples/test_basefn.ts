// Test isFunction and baseFunctions logic
function keys(obj: any): string[] {
    const result: string[] = [];
    for (const key in obj) {
        result.push(key);
    }
    return result;
}

function isFunction(value: any): boolean {
    const t = typeof value;
    console.log("isFunction typeof: " + t);
    return t === 'function';
}

function baseFunctions(object: any, props: string[]): string[] {
    console.log("baseFunctions: " + props.length + " props");
    const result: string[] = [];
    for (let i = 0; i < props.length; i++) {
        const key = props[i];
        console.log("Checking key: '" + key + "'");
        const val = object[key];
        console.log("Got val");
        if (isFunction(val)) {
            console.log("Is function!");
            result.push(key);
        }
    }
    return result;
}

const lodashMethods: any = {
    chunk: function(arr: any[], size: number) { return arr; },
    compact: function(arr: any[]) { return arr; }
};

console.log("Getting keys");
const props = keys(lodashMethods);
console.log("Keys: " + props.length);
for (let i = 0; i < props.length; i++) {
    console.log("  " + props[i]);
}
const methodNames = baseFunctions(lodashMethods, props);
console.log("Method names: " + methodNames.length);
