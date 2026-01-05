// Test 105.11.5.21: Lodash runInContext() Simulation (Mini Version)
// Pattern: Simulates the lodash method registration that causes hangs

var _ = (function() {
    var lodash = {};
    
    // Simulate lodash method registration (mini version of runInContext)
    var methodNames = ['chunk', 'compact', 'drop', 'filter', 'find', 
                       'first', 'flatten', 'head', 'initial', 'last',
                       'map', 'reduce', 'some', 'tail', 'take'];
    
    for (var i = 0; i < methodNames.length; i++) {
        var name = methodNames[i];
        lodash[name] = (function(methodName) {
            return function() { 
                return "called: " + methodName; 
            };
        })(name);
    }
    
    // Add a few actual implementations
    lodash.identity = function(value) {
        return value;
    };
    
    lodash.constant = function(value) {
        return function() { return value; };
    };
    
    return lodash;
})();

// Test the methods
console.log("_.chunk():", _.chunk());
console.log("_.filter():", _.filter());
console.log("_.map():", _.map());
console.log("_.identity(42):", _.identity(42));

var getHello = _.constant("hello");
console.log("_.constant('hello')():", getHello());

console.log("PASS: run_in_context");
