// Very basic test
function foo(a: number): number {
    return a + 1;
}

const r = foo(5);
console.log("foo(5) = " + r);
console.log("Direct: " + foo(5));
