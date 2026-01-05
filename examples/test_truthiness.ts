// Test truthiness of null and undefined
console.log('Testing truthiness...');

const testNull = null;
const testUndef = undefined;
const testZero = 0;
const testFalse = false;
const testEmptyStr = '';

console.log('null || "fallback":', testNull || 'fallback');
console.log('undefined || "fallback":', testUndef || 'fallback');
console.log('0 || "fallback":', testZero || 'fallback');
console.log('false || "fallback":', testFalse || 'fallback');
console.log('"" || "fallback":', testEmptyStr || 'fallback');

console.log('\nDirect tests:');
console.log('null || "A":', null || 'A');
console.log('undefined || "B":', undefined || 'B');
