// Test harness for Level 1
const result = require('./lodash_test_1_hoisting.js');

console.log('\n=== Level 1 Test Results ===');
console.log('typeof result:', typeof result);

if (typeof result === 'function') {
  console.log('✅ PASS: result is a function');
  console.log('Calling result():', result());
  
  if (typeof result._ === 'function') {
    console.log('✅ PASS: result._ is accessible');
  } else {
    console.log('❌ FAIL: result._ is', typeof result._);
  }
} else {
  console.log('❌ FAIL: result should be function, got', typeof result);
}
