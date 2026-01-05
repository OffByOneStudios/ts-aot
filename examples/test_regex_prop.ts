// Regex property access test
const re = /abc/;
console.log('typeof re:', typeof re);

// Try accessing .test property
const testFn = re.test;
console.log('typeof testFn:', typeof testFn);
