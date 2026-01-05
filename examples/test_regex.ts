// Test: RegExp methods
const re = /abc/;

console.log('typeof re:', typeof re);
console.log('typeof re.test:', typeof re.test);

console.log('re.test("xabcy"):', re.test('xabcy'));
console.log('re.test("xyz"):', re.test('xyz'));
