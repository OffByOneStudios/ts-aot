// Test WeakRef ES2021

// Test 1: WeakRef basic creation and deref
const obj1 = { name: "test", value: 42 };
const ref1 = new WeakRef(obj1);
const derefed1 = ref1.deref();
if (derefed1 !== undefined) {
    console.log("PASS: WeakRef.deref() returns target");
} else {
    console.log("FAIL: WeakRef.deref() returned undefined");
}

// Test 2: WeakRef deref returns consistently
const ref2 = new WeakRef(obj1);
const d1 = ref2.deref();
const d2 = ref2.deref();
if (d1 !== undefined && d2 !== undefined) {
    console.log("PASS: WeakRef.deref() returns target consistently");
} else {
    console.log("FAIL: WeakRef.deref() inconsistent");
}

// Test 3: WeakRef with different object types
const arr = [1, 2, 3];
const refArr = new WeakRef(arr);
if (refArr.deref() !== undefined) {
    console.log("PASS: WeakRef works with arrays");
} else {
    console.log("FAIL: WeakRef with array returned undefined");
}

// Test 4: Multiple WeakRefs to same target
const target = { id: 99 };
const refA = new WeakRef(target);
const refB = new WeakRef(target);
if (refA.deref() !== undefined && refB.deref() !== undefined) {
    console.log("PASS: Multiple WeakRefs to same target work");
} else {
    console.log("FAIL: Multiple WeakRefs failed");
}

console.log("WeakRef tests complete");

function user_main(): number {
    return 0;
}
