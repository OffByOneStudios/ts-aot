// Test || operator with object and function
const obj = { x: 1 };
const func = function() { return 42; };

const result1 = obj || func;
console.log('result1 typeof:', typeof result1);
console.log('result1 === obj:', result1 === obj);

const result2 = false || obj || func;
console.log('result2 typeof:', typeof result2);
console.log('result2 === obj:', result2 === obj);

const result3 = false || false || func;
console.log('result3 typeof:', typeof result3);
console.log('result3 === func:', result3 === func);

// Mimic lodash exactly
const freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
const freeSelf = false;  // Simulate self not existing
const root = freeGlobal || freeSelf || func;
console.log('root typeof:', typeof root);
console.log('root === freeGlobal:', root === freeGlobal);
console.log('root === func:', root === func);
