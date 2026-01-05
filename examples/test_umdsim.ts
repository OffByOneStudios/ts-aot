// Minimal repro of lodash-style UMD export assignment.

(function () {
  // These names mirror lodash.js
  const freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;
  const freeModule = freeExports && typeof module == 'object' && module && !module.nodeType && module;

  console.log('typeof freeExports:', typeof freeExports);
  console.log('typeof freeModule:', typeof freeModule);

  function runInContext() {
    function lodash() {
      return 123;
    }
    return lodash;
  }

  const _ = runInContext();

  console.log('typeof freeModule.exports (before):', typeof freeModule.exports);
  console.log('exports === freeModule.exports (before):', exports === freeModule.exports);

  // Lodash-style combined assignment-expression pattern.
  (freeModule.exports = _)._ = _;
  freeExports._ = _;

  console.log('typeof freeModule.exports (after):', typeof freeModule.exports);
  console.log('exports === freeModule.exports (after):', exports === freeModule.exports);

  console.log('typeof _:', typeof _);
  console.log('typeof module.exports:', typeof module.exports);
  console.log('typeof exports._:', typeof exports._);
})();
