// Test with just uniq - no rest parameters
import { uniq, uniqBy } from './src/array/uniq';

// Test uniq
const arr1 = [1, 2, 1, 3, 2, 4];
const unique = uniq(arr1);
console.log(unique.length); // Expected: 4 (1, 2, 3, 4)

// Test uniqBy - unique by modulo 3 result
const arr2 = [1, 2, 3, 4, 5];
const uniqueByMod = uniqBy(arr2, (n: number) => n % 3);
console.log(uniqueByMod.length); // Expected: 3 (0, 1, 2 remainders)

console.log("Done!");
