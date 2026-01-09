// Test Function.prototype.bind - partial application and this binding

function user_main(): number {
    // Test 1: Partial application with one bound arg
    function add(a: number, b: number): number {
        return a + b;
    }
    const add5 = add.bind(null, 5);
    const r1 = add5(3);  // Should be 8
    console.log("Test 1 - partial app:");
    console.log(r1);

    // Test 2: Partial application with two bound args
    function mul(a: number, b: number, c: number): number {
        return a * b * c;
    }
    const mul2x3 = mul.bind(null, 2, 3);
    const r2 = mul2x3(4);  // Should be 24
    console.log("Test 2 - two bound args:");
    console.log(r2);

    // Test 3: Chained binding (bind a bound function)
    const add10 = add.bind(null, 10);
    const add10and5 = add10.bind(null);  // Just rebind with same this
    const r3 = add10and5(7);  // Should be 17
    console.log("Test 3 - chained bind:");
    console.log(r3);

    // Test 4: `this` binding
    console.log("Test 4 - this binding:");
    const obj = {
        value: 42,
        getValue: function(): number {
            return (this as any).value;
        }
    };
    const getValue = obj.getValue;
    const boundGetValue = obj.getValue.bind(obj);

    // Direct call should work
    console.log(obj.getValue());  // Should print 42

    // Bound call should also work
    console.log(boundGetValue());  // Should print 42

    return 0;
}
