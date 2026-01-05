// Test 1: Simplest variable assignment in IIFE
;(function() {
  console.log('[Test1] Start');
  var x = 42;
  console.log('[Test1] x =', x);
  
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = x;
  }
}.call(this));
