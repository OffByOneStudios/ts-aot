const result = require('./test_store_1_simple.js');
console.log('Result:', result, 'typeof:', typeof result);
console.log(result === 42 ? '✅ PASS' : '❌ FAIL - expected 42');
