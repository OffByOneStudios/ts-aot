// Level 1: Test variable hoisting and forward references
;(function() {
  console.log('[Level1] IIFE start');
  
  // Forward reference - using `result` before it's declared
  console.log('[Level1] Early typeof result:', typeof result);
  
  // Multiple scattered var declarations (should hoist)
  var x = 1;
  
  function runInContext() {
    console.log('[Level1] runInContext start');
    
    // More forward references
    console.log('[Level1] Inner y before decl:', typeof y);
    
    function innerFunc() {
      console.log('[Level1] innerFunc called');
      // Reference variable declared later
      return z + 1;
    }
    
    var y = 10;
    var z = 20;
    
    console.log('[Level1] runInContext returning innerFunc');
    return innerFunc;
  }
  
  var a = 5;
  
  // Main execution - call runInContext
  console.log('[Level1] Calling runInContext');
  var result = runInContext();
  console.log('[Level1] After runInContext, typeof result:', typeof result);
  
  var b = 7;
  
  // UMD export
  var freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;
  var freeModule = typeof module == 'object' && module && !module.nodeType && module;
  
  console.log('[Level1] freeModule:', typeof freeModule);
  
  if (freeModule) {
    console.log('[Level1] Exporting via CommonJS');
    (freeModule.exports = result)._ = result;
  }
  
  console.log('[Level1] IIFE end');
}.call(this));
