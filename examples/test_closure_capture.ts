// Test closure capture in arrow functions

function testClosure(): number {
    let x = 10;
    let y = 20;
    
    // This arrow function should capture x and y
    const add = (n: number) => n + x + y;
    
    return add(5);  // Should return 35 (5 + 10 + 20)
}

console.log("Testing closure capture:");
const result = testClosure();
console.log(result);

// Test nested closure - outer function captures a function parameter
function callWithIteratee(iteratee: (n: number) => number): number {
    const a = 3;
    const b = 7;
    
    // Inner arrow function captures 'iteratee' from outer scope
    const compare = (x: number, y: number) => iteratee(x) - iteratee(y);
    
    return compare(a, b);  // Should return iteratee(3) - iteratee(7)
}

console.log("Testing nested function capture:");
const result2 = callWithIteratee((n: number) => n * 2);
console.log(result2);  // Should be (3*2) - (7*2) = 6 - 14 = -8

console.log("All closure tests passed!");
