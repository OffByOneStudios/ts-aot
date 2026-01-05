// Test null/undefined string operations
console.log('Test 1: typeof on null/undefined');
const nullVal = null;
const undefVal = undefined;
console.log('typeof null:', typeof nullVal);
console.log('typeof undefined:', typeof undefVal);

console.log('\nTest 2: Property access on null/undefined');
const obj1 = null;
const obj2 = undefined;
// These should not crash, just return undefined
const prop1 = (obj1 as any)?.x;
const prop2 = (obj2 as any)?.y;
console.log('null?.x:', prop1);
console.log('undefined?.y:', prop2);

console.log('\nTest 3: Logical OR with null/undefined');
const val1 = null || 'default1';
const val2 = undefined || 'default2';
console.log('null || default:', val1);
console.log('undefined || default:', val2);

console.log('\nAll tests passed!');
