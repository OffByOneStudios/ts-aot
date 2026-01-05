;(function() {
  
  // Detect free variable exports
  var freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;
  
  // Detect free variable module
  var freeModule = freeExports && typeof module == 'object' && module && !module.nodeType && module;
  
  console.log('[lodash_sim] freeExports:', typeof freeExports);
  console.log('[lodash_sim] freeModule:', typeof freeModule);
  
  // Main lodash function
  function lodash(value) {
    return 'lodash called with: ' + value;
  }
  
  // Mimic runInContext pattern
  function runInContext() {
    console.log('[lodash_sim] runInContext called');
    
    function innerLodash(value) {
      return 'innerLodash: ' + value;
    }
    
    console.log('[lodash_sim] runInContext returning:', typeof innerLodash);
    return innerLodash;
  }
  
  // This mimics lodash line 17182: var _ = runInContext();
  var _ = runInContext();
  
  console.log('[lodash_sim] After runInContext, typeof _:', typeof _);
  console.log('[lodash_sim] _ value:', _);
  
  // Export using lodash pattern: (freeModule.exports = _)._ = _;
  if (freeModule) {
    console.log('[lodash_sim] Before export, typeof freeModule.exports:', typeof freeModule.exports);
    (freeModule.exports = _)._ = _;
    console.log('[lodash_sim] After export, typeof freeModule.exports:', typeof freeModule.exports);
  }

}.call(this));
