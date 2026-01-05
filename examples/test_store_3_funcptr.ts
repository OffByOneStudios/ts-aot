const result = require('./test_store_3_funcptr.js');
console.log('Result typeof:', typeof result);
console.log(typeof result === 'function' ? '✅ PASS' : '❌ FAIL - expected function');
if (typeof result === 'function') {
  console.log('Calling result():', result());
}
