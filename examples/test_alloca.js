// Test if alloca variables work for function storage
console.log('[alloca_test] Starting');

function makeFunc() {
  console.log('[alloca_test] makeFunc called');
  function inner() {
    return 42;
  }
  console.log('[alloca_test] Returning inner');
  return inner;
}

// Use a block scope to force alloca instead of global
{
  var result = makeFunc();
  console.log('[alloca_test] result type:', typeof result);
  console.log('[alloca_test] result:', result);
  
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = result;
  }
}
