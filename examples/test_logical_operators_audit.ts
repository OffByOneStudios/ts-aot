// Comprehensive audit of all logical operators
console.log('=== LOGICAL AND (&&) ===');

// Test 1: && with object and boolean
const obj1 = { x: 1 };
const result1 = obj1 && true;
console.log('obj1 && true:', typeof result1, result1 === true);

const result2 = true && obj1;
console.log('true && obj1:', typeof result2, result2 === obj1);

// Test 2: && with falsy values
const result3 = false && obj1;
console.log('false && obj1:', typeof result3, result3 === false);

const result4 = 0 && obj1;
console.log('0 && obj1:', typeof result4, result4 === 0);

const result5 = null && obj1;
console.log('null && obj1:', typeof result5, result5 === null);

// Test 3: Chained &&
const result6 = true && obj1 && 42;
console.log('true && obj1 && 42:', typeof result6, result6 === 42);

console.log('=== LOGICAL OR (||) ===');

// Test 4: || with object and boolean
const result7 = obj1 || true;
console.log('obj1 || true:', typeof result7, result7 === obj1);

const result8 = false || obj1;
console.log('false || obj1:', typeof result8, result8 === obj1);

// Test 5: || with truthy values
const result9 = 42 || obj1;
console.log('42 || obj1:', typeof result9, result9 === 42);

// Test 6: Chained ||
const result10 = false || 0 || obj1 || 'never';
console.log('false || 0 || obj1 || never:', typeof result10, result10 === obj1);

console.log('=== NULLISH COALESCING (??) ===');

// Test 7: ?? with null/undefined
const result11 = null ?? obj1;
console.log('null ?? obj1:', typeof result11, result11 === obj1);

const result12 = undefined ?? obj1;
console.log('undefined ?? obj1:', typeof result12, result12 === obj1);

// Test 8: ?? with falsy but defined values
const result13 = 0 ?? obj1;
console.log('0 ?? obj1:', typeof result13, result13 === 0);

const result14 = false ?? obj1;
console.log('false ?? obj1:', typeof result14, result14 === false);

const result15 = '' ?? obj1;
console.log('"" ?? obj1:', typeof result15, result15 === '');

console.log('=== TERNARY (?:) ===');

// Test 9: Ternary with different types
const result16 = true ? obj1 : 'never';
console.log('true ? obj1 : never:', typeof result16, result16 === obj1);

const result17 = false ? 'never' : obj1;
console.log('false ? never : obj1:', typeof result17, result17 === obj1);

// Test 10: Ternary with complex condition
const result18 = (obj1 && true) ? 42 : 'never';
console.log('(obj1 && true) ? 42 : never:', typeof result18, result18 === 42);

console.log('=== MIXED OPERATORS ===');

// Test 11: && and || mixed
const result19 = false && obj1 || 42;
console.log('false && obj1 || 42:', typeof result19, result19 === 42);

const result20 = obj1 && false || 'fallback';
console.log('obj1 && false || fallback:', typeof result20, result20 === 'fallback');

// Test 12: Global object test (the problematic case)
const freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
console.log('freeGlobal:', typeof freeGlobal, freeGlobal === global);

const freeSelf = false;
const func = function() { return 1; };
const root = freeGlobal || freeSelf || func;
console.log('root from (freeGlobal || freeSelf || func):', typeof root, root === global);

console.log('=== ALL TESTS COMPLETE ===');
