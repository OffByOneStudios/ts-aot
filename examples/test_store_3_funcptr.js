// Test 3: Function pointer assignment in IIFE
;(function() {
  console.log('[Test3] Start');
  
  function inner() {
    return 42;
  }
  
  var result = inner;
  console.log('[Test3] result typeof:', typeof result);
  
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = result;
  }
}.call(this));
