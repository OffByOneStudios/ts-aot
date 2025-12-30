// Call uniq
import { uniq } from './src/array/uniq';

const arr = [1, 2, 1, 3, 2, 4];
const unique = uniq(arr);
console.log(unique.length);
console.log("Done!");
