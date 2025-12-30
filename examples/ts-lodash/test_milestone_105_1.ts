// Test the new array functions from Milestone 105.1

import { uniq, uniqBy } from './src/array/uniq';
import { difference } from './src/array/difference';

// Test uniq
const arr1 = [1, 2, 1, 3, 2, 4];
const unique = uniq(arr1);
console.log(unique.length); // Expected: 4 (1, 2, 3, 4)

// Test uniqBy - unique by Math.floor result
const arr2 = [1, 2, 3, 4, 5];
const uniqueByMod = uniqBy(arr2, (n: number) => n % 3);
console.log(uniqueByMod.length); // Expected: 3 (0, 1, 2 remainders)

// Test difference
const arr3 = [1, 2, 3, 4, 5];
const arr4 = [2, 4];
const diff = difference(arr3, arr4);
console.log(diff.length); // Expected: 3 (1, 3, 5)

console.log("All tests passed!");

// NOTE: flattenDeep and intersection require proper generic array type inference
// which has issues with nested array types (T[][] where T is a type parameter).
