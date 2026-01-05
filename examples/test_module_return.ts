// Test what the module init returns
console.log('[test_module_return] Starting');

// Import the module via codegen (this will call __module_init_xxx)
const moduleInitResult = (() => {
    // This will be replaced by actual module init call during codegen
    return require('./lodash_test_minimal.js');
})();

console.log('[test_module_return] Module init returned:', typeof moduleInitResult);
console.log('[test_module_return] Value:', moduleInitResult);
