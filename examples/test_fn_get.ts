// Test: getting a function from an 'any' object and calling it

// Create an object with a function property  
const obj = {
    identity: function(x: number): number {
        console.log('identity called with', x);
        return x;
    }
};

console.log('Test 1: Direct obj.identity(2)');
const r1 = obj.identity(2);
console.log('r1 =', r1);

console.log('');

// Now try getting the function via property and calling it
const anyObj: any = obj;
const fn = anyObj.identity;  // Get function from 'any' object

console.log('typeof fn:', typeof fn);

console.log('Test 2: fn(2)');
const r2 = fn(2);  // Call it
console.log('r2 =', r2);
