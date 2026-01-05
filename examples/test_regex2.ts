// Full regex test
const re = /abc/;

console.log('typeof re:', typeof re);

// Access and test
console.log('re.test("xabcy"):', re.test('xabcy'));
console.log('re.test("xyz"):', re.test('xyz'));
