// Test: accessing a function via property access and calling it

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

// Now try accessing through any
const anyObj: any = obj;

console.log('Test 2: anyObj.identity(2)');
const r2 = anyObj.identity(2);
console.log('r2 =', r2);

console.log('');

// Wrapper that accesses through any
function wrapper(o: any): any {
    console.log('wrapper: calling o.identity(3)');
    const result = o.identity(3);
    console.log('wrapper: result =', result);
    return result;
}

console.log('Test 3: wrapper(obj)');
const r3 = wrapper(obj);
console.log('r3 =', r3);
