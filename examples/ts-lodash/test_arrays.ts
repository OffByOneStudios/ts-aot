/**
 * Test ts-lodash non-callback functions
 */
import { 
    // Array
    chunk, flatten, take, drop, head, last, tail,
    // Util
    identity, times, range
} from './index';

// Test identity
console.log("=== Identity ===");
const num = identity(42);
console.log(num);

const str = identity("hello");
console.log(str);

// Test range
console.log("=== Range ===");
const r1 = range(5);
console.log("range(5):");
let i = 0;
while (i < r1.length) {
    console.log(r1[i]);
    i = i + 1;
}

// Test times
console.log("=== Times ===");
const indices = times(5, (idx) => idx);
console.log("times(5, i => i):");
i = 0;
while (i < indices.length) {
    console.log(indices[i]);
    i = i + 1;
}

// Test head/last
console.log("=== Head/Last ===");
const arr = [10, 20, 30, 40, 50];
console.log("head([10,20,30,40,50]):");
console.log(head(arr));

console.log("last([10,20,30,40,50]):");
console.log(last(arr));

// Test tail
console.log("=== Tail ===");
const tailed = tail(arr);
console.log("tail([10,20,30,40,50]):");
i = 0;
while (i < tailed.length) {
    console.log(tailed[i]);
    i = i + 1;
}

// Test take
console.log("=== Take ===");
const taken = take(arr, 3);
console.log("take([10,20,30,40,50], 3):");
i = 0;
while (i < taken.length) {
    console.log(taken[i]);
    i = i + 1;
}

// Test drop  
console.log("=== Drop ===");
const dropped = drop(arr, 2);
console.log("drop([10,20,30,40,50], 2):");
i = 0;
while (i < dropped.length) {
    console.log(dropped[i]);
    i = i + 1;
}

// Test chunk
console.log("=== Chunk ===");
const letters = ["a", "b", "c", "d", "e"];
const chunked = chunk(letters, 2);
console.log("chunk(['a','b','c','d','e'], 2):");
i = 0;
while (i < chunked.length) {
    const c = chunked[i];
    console.log("  chunk:");
    let j = 0;
    while (j < c.length) {
        console.log(c[j]);
        j = j + 1;
    }
    i = i + 1;
}

// Test flatten
console.log("=== Flatten ===");
const nested: number[][] = [[1, 2], [3, 4], [5]];
const flat = flatten(nested);
console.log("flatten([[1,2],[3,4],[5]]):");
i = 0;
while (i < flat.length) {
    console.log(flat[i]);
    i = i + 1;
}

console.log("=== ts-lodash array test complete! ===");
