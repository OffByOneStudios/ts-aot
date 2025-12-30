/**
 * Simple test of ts-lodash functions
 */
import { 
    // Collection
    filter, map, reduce, find, findIndex, every, some, forEach,
    // Util
    identity, times
} from './index';

// Test identity
console.log("=== Identity ===");
const num = identity(42);
console.log(num);

const str = identity("hello");
console.log(str);

// Test times
console.log("=== Times ===");
const squares = times(5, (i) => i * i);
console.log("times(5, i => i*i):");
forEach(squares, (n) => console.log(n));

// Test filter
console.log("=== Filter ===");
const nums = [1, 2, 3, 4, 5];
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

console.log("=== Simple ts-lodash test complete! ===");
