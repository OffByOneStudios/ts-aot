// Test any-typed string comparison

function anyEq(a: any, b: any): boolean {
    return a === b;
}

console.log("anyEq(1, 1):", anyEq(1, 1));
console.log("anyEq(1, 2):", anyEq(1, 2));
console.log("anyEq('hello', 'hello'):", anyEq("hello", "hello"));
console.log("anyEq('hello', 'world'):", anyEq("hello", "world"));
