// Test 5: Nested function return (like returnsFunction pattern)
;(function() {
  console.log('[Test5] Start');
  
  function outer() {
    function inner() {
      return 42;
    }
    return inner;
  }
  
  var result = outer();
  console.log('[Test5] result typeof:', typeof result);
  
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = result;
  }
}.call(this));
