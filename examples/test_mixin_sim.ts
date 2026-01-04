// Simulate lodash mixin pattern
console.log("Start");

function identity(x: any): any { return x; }

// Create a base object with many functions
const lodash: any = {};

// Add 300 functions (like lodash does)
const methodNames = [
    "chunk", "compact", "concat", "difference", "drop", "dropRight",
    "fill", "findIndex", "findLastIndex", "flatten", "flattenDeep",
    "head", "indexOf", "initial", "intersection", "join", "last",
    "lastIndexOf", "nth", "pull", "pullAll", "pullAt", "remove",
    "reverse", "slice", "sortedIndex", "sortedIndexOf", "sortedLastIndex",
    "sortedUniq", "tail", "take", "takeRight", "union", "uniq",
    "without", "xor", "zip", "zipObject"
];

for (let i = 0; i < methodNames.length; i = i + 1) {
    lodash[methodNames[i]] = identity;
}

console.log("Base methods added:", Object.keys(lodash).length);

// Simulate mixin - for each method, create wrapper
const prototype: any = {};
const keys = Object.keys(lodash);
console.log("Mixing in", keys.length, "methods");

for (let i = 0; i < keys.length; i = i + 1) {
    const methodName = keys[i];
    const func = lodash[methodName];
    
    // Create wrapper function
    prototype[methodName] = function(): any {
        return func.apply(lodash, arguments);
    };
}

lodash.prototype = prototype;

console.log("Mixin complete, prototype methods:", Object.keys(prototype).length);
console.log("Done");
