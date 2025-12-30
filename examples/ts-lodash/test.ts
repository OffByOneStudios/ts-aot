/**
 * Test ts-lodash functions
 */
import { 
    // Array
    chunk, flatten, take, drop, head, last, tail,
    // Collection
    filter, map, reduce, find, findIndex, every, some, forEach,
    // Util
    identity, times, range, rangeFromTo
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
forEach(r1, (n) => console.log(n));

const r2 = rangeFromTo(2, 6);
console.log("rangeFromTo(2, 6):");
forEach(r2, (n) => console.log(n));

// Test times
console.log("=== Times ===");
const squares = times(5, (i) => i * i);
console.log("times(5, i => i*i):");
forEach(squares, (n) => console.log(n));

// Test chunk
console.log("=== Chunk ===");
const letters = ["a", "b", "c", "d", "e"];
const chunked = chunk(letters, 2);
console.log("chunk(['a','b','c','d','e'], 2):");
let i = 0;
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
forEach(flat, (n) => console.log(n));

// Test take/drop
console.log("=== Take/Drop ===");
const nums = [1, 2, 3, 4, 5];

const taken = take(nums, 3);
console.log("take([1,2,3,4,5], 3):");
forEach(taken, (n) => console.log(n));

const dropped = drop(nums, 2);
console.log("drop([1,2,3,4,5], 2):");
forEach(dropped, (n) => console.log(n));

// Test head/last/tail
console.log("=== Head/Last/Tail ===");
console.log("head([1,2,3,4,5]):");
console.log(head(nums));

console.log("last([1,2,3,4,5]):");
console.log(last(nums));

const tailed = tail(nums);
console.log("tail([1,2,3,4,5]):");
forEach(tailed, (n) => console.log(n));

// Test filter
console.log("=== Filter ===");
const evens = filter(nums, (n) => n % 2 === 0);
console.log("filter([1,2,3,4,5], n => n % 2 === 0):");
forEach(evens, (n) => console.log(n));

// Test map
console.log("=== Map ===");
const doubled = map(nums, (n) => n * 2);
console.log("map([1,2,3,4,5], n => n * 2):");
forEach(doubled, (n) => console.log(n));

// Test reduce
console.log("=== Reduce ===");
const sum = reduce(nums, (acc, n) => acc + n, 0);
console.log("reduce([1,2,3,4,5], (acc,n) => acc+n, 0):");
console.log(sum);

// Test find
console.log("=== Find ===");
const found = find(nums, (n) => n > 3);
console.log("find([1,2,3,4,5], n => n > 3):");
console.log(found);

const foundIdx = findIndex(nums, (n) => n > 3);
console.log("findIndex([1,2,3,4,5], n => n > 3):");
console.log(foundIdx);

// Test every/some
console.log("=== Every/Some ===");
const allPositive = every(nums, (n) => n > 0);
console.log("every([1,2,3,4,5], n => n > 0):");
console.log(allPositive);

const hasEven = some(nums, (n) => n % 2 === 0);
console.log("some([1,2,3,4,5], n => n % 2 === 0):");
console.log(hasEven);

console.log("=== ts-lodash test complete! ===");
