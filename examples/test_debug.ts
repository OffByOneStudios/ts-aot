import * as lodash from './lodash.js';

function testAdd(a: number, b: number): number {
    const result = a + b;
    console.log("Result:", result);
    return result;
}

const x = testAdd(5, 3);
console.log("Final:", x);
