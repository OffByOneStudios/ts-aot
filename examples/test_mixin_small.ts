// Test the exact mixin function from lodash with small object
function keys(obj: any): string[] {
    const result: string[] = [];
    for (const key in obj) {
        result.push(key);
    }
    return result;
}

function isFunction(value: any): boolean {
    return typeof value === 'function';
}

function baseFunctions(object: any, props: string[]): string[] {
    const result: string[] = [];
    for (let i = 0; i < props.length; i++) {
        const key = props[i];
        if (isFunction(object[key])) {
            result.push(key);
        }
    }
    return result;
}

function mixin(object: any, source: any): any {
    const props = keys(source);
    console.log("mixin: got " + props.length + " props");
    const methodNames = baseFunctions(source, props);
    console.log("mixin: found " + methodNames.length + " methods");
    return object;
}

// Use exact same object structure as test_mixin_leak.ts (with rest params)
const lodashMethods = {
    chunk: function(arr: any[], size: number) { return arr; },
    compact: function(arr: any[]) { return arr; },
    concat: function(...args: any[]) { return args; },
    difference: function(arr: any[], ...values: any[]) { return arr; }
};

console.log("Start");
const lodash: any = {};
mixin(lodash, lodashMethods);
console.log("Done");
