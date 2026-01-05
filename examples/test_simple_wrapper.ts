// Minimal test: calling a function through an 'any'-typed wrapper

// A simple function that returns its argument
function identity(x: number): number {
    return x;
}

// Wrapper that takes 'any' and returns 'any'
function wrapper(v: any): any {
    console.log('wrapper called with v =', v);
    console.log('typeof v =', typeof v);
    
    // Call identity which expects number
    const result = identity(v);
    
    console.log('identity returned:', result);
    console.log('typeof result:', typeof result);
    return result;
}

console.log('Test 1: Direct identity(2)');
const r1 = identity(2);
console.log('r1 =', r1);

console.log('');
console.log('Test 2: wrapper(2)');
const r2 = wrapper(2);
console.log('r2 =', r2);
