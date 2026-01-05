// Test 4: Same as test 3 but WITHOUT .call(this)
;(function() {
  console.log('[Test4] Start');
  
  function inner() {
    return 42;
  }
  
  var result = inner;
  console.log('[Test4] result typeof:', typeof result);
  
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = result;
  }
})();
