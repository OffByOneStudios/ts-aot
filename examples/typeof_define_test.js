// Test typeof undefined variable
console.log("typeof define:", typeof define);
console.log("typeof undeclared:", typeof undeclared_var_xyz);

// Check truthiness 
if (typeof define == 'function') {
    console.log("define is a function - BAD");
} else {
    console.log("define is NOT a function - GOOD");
}

// Also test the actual condition lodash uses
var defineLike = typeof define == 'function' && typeof define.amd == 'object' && define.amd;
console.log("defineLike:", defineLike ? "truthy" : "falsy");
