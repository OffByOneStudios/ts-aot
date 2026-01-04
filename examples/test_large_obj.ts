// Test passing larger static object to any parameter (55 methods like lodash)
function keys(obj: any): string[] {
    const result: string[] = [];
    for (const key in obj) {
        result.push(key);
    }
    return result;
}

const lodashMethods = {
    chunk: function(arr: any[], size: number) { return arr; },
    compact: function(arr: any[]) { return arr; },
    concat: function(...args: any[]) { return args; },
    difference: function(arr: any[], values: any[]) { return arr; },
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
    intersection: function(arrs: any[][]) { return []; },
    join: function(arr: any[], separator: string) { return ""; },
    last: function(arr: any[]) { return arr[arr.length - 1]; },
    lastIndexOf: function(arr: any[], value: any) { return 0; },
    nth: function(arr: any[], n: number) { return arr[n]; },
    pull: function(arr: any[], values: any[]) { return arr; },
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
    union: function(arrs: any[][]) { return []; },
    unionBy: function(args: any[]) { return []; },
    unionWith: function(args: any[]) { return []; },
    uniq: function(arr: any[]) { return arr; },
    uniqBy: function(arr: any[], iteratee: any) { return arr; },
    uniqWith: function(arr: any[], comparator: any) { return arr; },
    unzip: function(arr: any[]) { return arr; },
    unzipWith: function(arr: any[], iteratee: any) { return arr; },
    without: function(arr: any[], values: any[]) { return arr; },
    xor: function(arrs: any[][]) { return []; },
    xorBy: function(args: any[]) { return []; },
    xorWith: function(args: any[]) { return []; },
    zip: function(arrs: any[][]) { return []; },
    zipObject: function(props: any[], values: any[]) { return {}; },
    zipObjectDeep: function(props: any[], values: any[]) { return {}; },
    zipWith: function(args: any[]) { return []; }
};

console.log("Getting keys");
const k = keys(lodashMethods);
console.log("Found " + k.length + " keys");
for (let i = 0; i < 5; i++) {
    console.log("  " + k[i]);
}
console.log("Done");
