const result = require('./test_alloca.js');

console.log('typeof result:', typeof result);
if (typeof result === 'function') {
  console.log('✅ PASS - calling result():', result());
} else {
  console.log('❌ FAIL - expected function, got:', typeof result);
}
