// Test cell variable patterns similar to lodash
function wrapper() {
    function inner1() { return 1; }
    function inner2() { return inner1() + 1; }  // Forward reference
    
    // Return an object with function references
    return {
        a: inner1,
        b: inner2
    };
}

const result = wrapper();
console.log("typeof result.a:", typeof result.a);
console.log("typeof result.b:", typeof result.b);
console.log("result.a():", result.a());
console.log("result.b():", result.b());
