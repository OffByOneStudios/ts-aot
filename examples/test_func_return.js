// Simple test: function that returns another function

console.log('[test_func_return] Starting');

function makeFunc() {
    console.log('[test_func_return] makeFunc called');
    function inner() {
        console.log('[test_func_return] inner called');
        return 42;
    }
    console.log('[test_func_return] returning inner');
    return inner;
}

console.log('[test_func_return] Calling makeFunc');
var result = makeFunc();
console.log('[test_func_return] result type:', typeof result);
console.log('[test_func_return] result value:', result);

if (typeof module !== 'undefined' && module.exports) {
    module.exports = result;
}
