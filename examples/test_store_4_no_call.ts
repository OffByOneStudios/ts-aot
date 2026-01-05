const result = require('./test_store_4_no_call.js');
console.log('Result typeof:', typeof result);
console.log(typeof result === 'function' ? '✅ PASS' : '❌ FAIL - expected function');
if (typeof result === 'function') {
  console.log('Calling result():', result());
}
