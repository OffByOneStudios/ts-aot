// Test: calling a function stored in a map/object via any

// Create a map and store a function
const obj: any = {};
obj.myFunc = function(x: number): number {
    console.log('myFunc called with', x);
    return x * 2;
};

console.log('Test 1: obj.myFunc(5)');
const fn = obj.myFunc;
console.log('typeof fn:', typeof fn);

const result = fn(5);
console.log('result:', result);
