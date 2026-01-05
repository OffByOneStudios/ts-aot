// Direct test
const result1 = null || 'should be fallback';
const result2 = 'should be fallback' || null;

console.log('Test 1 result:');
console.log(result1);
console.log('Test 1 type:', typeof result1);

console.log('\nTest 2 result:');
console.log(result2);
console.log('Test 2 type:', typeof result2);
