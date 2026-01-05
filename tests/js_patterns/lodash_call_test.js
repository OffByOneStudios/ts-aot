// Test if calling a function with the any-typed `this` crashes
;(function() {
  var undefined;
  
  var freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
  var freeSelf = typeof self == 'object' && self && self.Object === Object && self;
  var root = freeGlobal || freeSelf || Function('return this')();
  
  // Simulate lodash: create a function and call it with .call()
  var outerFunc = function(context) {
    console.log("Context type:", typeof context);
    
    // Accessing properties on context  
    var Array = context.Array;
    var Object = context.Object;
    
    console.log("Array:", typeof Array);
    console.log("Object:", typeof Object);
    
    return function lodash(value) {
      return value;
    };
  };
  
  // This is how lodash creates runInContext
  var runInContext = (function runInContext(context) {
    context = context == null ? root : context;
    return outerFunc.call(this, context);
  });
  
  console.log("Creating lodash instance...");
  var _ = runInContext();
  
  console.log("_ type:", typeof _);
  
  // Export
  root._ = _;
  
  console.log("PASS: lodash_call_test");
}).call(this);
