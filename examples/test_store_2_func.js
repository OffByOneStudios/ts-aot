// Test 2: Function assignment in IIFE
;(function() {
  console.log('[Test2] Start');
  
  function makeFunc() {
    return 42;
  }
  
  var result = makeFunc();
  console.log('[Test2] result =', result);
  
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = result;
  }
}.call(this));
