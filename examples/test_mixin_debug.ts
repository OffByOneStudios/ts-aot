// Debug: test what happens to the mixin source parameter
function keys(obj: any): string[] {
    console.log("keys: entering");
    const result: string[] = [];
    for (const key in obj) {
        console.log("keys: got key '" + key + "'");
        result.push(key);
    }
    console.log("keys: returning " + result.length + " keys");
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

function arrayEach(array: any[], iteratee: (value: any, index: number) => void): any[] {
    let index = -1;
    const length = array.length;
    while (++index < length) {
        if (iteratee(array[index], index) === false) {
            break;
        }
    }
    return array;
}

function mixin(object: any, source: any): any {
    console.log("mixin: calling keys(source)");
    const props = keys(source);
    console.log("mixin: got " + props.length + " props");
    return object;
}

const lodashMethods = {
    chunk: function(arr: any[], size: number) { return arr; },
    compact: function(arr: any[]) { return arr; },
    concat: function(...args: any[]) { return args; },
    difference: function(arr: any[], ...values: any[]) { return arr; },
    drop: function(arr: any[], n: number) { return arr; },
    dropRight: function(arr: any[], n: number) { return arr; },
    fill: function(arr: any[], value: any) { return arr; },
    findIndex: function(arr: any[], predicate: any) { return 0; },
    findLastIndex: function(arr: any[], predicate: any) { return 0; },
    first: function(arr: any[]) { return arr[0]; },
    flatten: function(arr: any[]) { return arr; },
    flattenDeep: function(arr: any[]) { return arr; },
    flattenDepth: function(arr: any[], depth: number) { return arr; },
    fromPairs: function(pairs: any[][]) { return {}; },
    head: function(arr: any[]) { return arr[0]; },
    indexOf: function(arr: any[], value: any) { return 0; },
    initial: function(arr: any[]) { return arr; },
    intersection: function(...arrays: any[][]) { return []; },
    join: function(arr: any[], separator: string) { return ""; },
    last: function(arr: any[]) { return arr[arr.length - 1]; },
    lastIndexOf: function(arr: any[], value: any) { return 0; },
    nth: function(arr: any[], n: number) { return arr[n]; },
    pull: function(arr: any[], ...values: any[]) { return arr; },
    pullAll: function(arr: any[], values: any[]) { return arr; },
    pullAt: function(arr: any[], indexes: any[]) { return []; },
    remove: function(arr: any[], predicate: any) { return []; },
    reverse: function(arr: any[]) { return arr; },
    slice: function(arr: any[], start: number, end: number) { return arr; },
    sortedIndex: function(arr: any[], value: any) { return 0; },
    sortedIndexBy: function(arr: any[], value: any, iteratee: any) { return 0; },
    sortedIndexOf: function(arr: any[], value: any) { return 0; },
    sortedLastIndex: function(arr: any[], value: any) { return 0; },
    sortedLastIndexBy: function(arr: any[], value: any, iteratee: any) { return 0; },
    sortedLastIndexOf: function(arr: any[], value: any) { return 0; },
    sortedUniq: function(arr: any[]) { return arr; },
    sortedUniqBy: function(arr: any[], iteratee: any) { return arr; },
    tail: function(arr: any[]) { return arr; },
    take: function(arr: any[], n: number) { return arr; },
    takeRight: function(arr: any[], n: number) { return arr; },
    takeRightWhile: function(arr: any[], predicate: any) { return arr; },
    takeWhile: function(arr: any[], predicate: any) { return arr; },
    union: function(...arrays: any[][]) { return []; },
    unionBy: function(...args: any[]) { return []; },
    unionWith: function(...args: any[]) { return []; },
    uniq: function(arr: any[]) { return arr; },
    uniqBy: function(arr: any[], iteratee: any) { return arr; },
    uniqWith: function(arr: any[], comparator: any) { return arr; },
    unzip: function(arr: any[]) { return arr; },
    unzipWith: function(arr: any[], iteratee: any) { return arr; },
    without: function(arr: any[], ...values: any[]) { return arr; },
    xor: function(...arrays: any[][]) { return []; },
    xorBy: function(...args: any[]) { return []; },
    xorWith: function(...args: any[]) { return []; },
    zip: function(...arrays: any[][]) { return []; },
    zipObject: function(props: any[], values: any[]) { return {}; },
    zipObjectDeep: function(props: any[], values: any[]) { return {}; },
    zipWith: function(...args: any[]) { return []; }
};

console.log("Start test");
const lodash: any = {};
mixin(lodash, lodashMethods);
console.log("Done");
