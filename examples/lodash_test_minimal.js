// Minimal test: Does function return work in JS slow path?
;(function() {
  console.log('[Minimal] Start');
  
  function returnsFunction() {
    console.log('[Minimal] returnsFunction called');
    function inner() {
      return 42;
    }
    console.log('[Minimal] Returning inner, typeof:', typeof inner);
    return inner;
  }
  
  console.log('[Minimal] Calling returnsFunction');
  var result = returnsFunction();
  console.log('[Minimal] After call, result type:', typeof result);
  console.log('[Minimal] result value:', result);
  
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = result;
  }
}.call(this));
