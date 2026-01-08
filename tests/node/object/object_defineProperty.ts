// Test Object.defineProperty, defineProperties, getOwnPropertyDescriptor

// Test 1: Object.defineProperty with value
const obj1: { x?: number } = {};
Object.defineProperty(obj1, 'x', { value: 42 });
if (obj1.x === 42) {
    console.log("PASS: Object.defineProperty sets value");
} else {
    console.log("FAIL: Object.defineProperty doesn't set value");
}

// Test 2: Object.defineProperty returns the object
const obj2: { y?: number } = {};
const result = Object.defineProperty(obj2, 'y', { value: 10 });
if (result === obj2) {
    console.log("PASS: Object.defineProperty returns same object");
} else {
    console.log("FAIL: Object.defineProperty doesn't return same object");
}

// Test 3: Object.defineProperties with multiple properties
const obj3: { a?: number; b?: number } = {};
Object.defineProperties(obj3, {
    a: { value: 1 },
    b: { value: 2 }
});
if (obj3.a === 1 && obj3.b === 2) {
    console.log("PASS: Object.defineProperties sets multiple values");
} else {
    console.log("FAIL: Object.defineProperties doesn't set values correctly");
}

// Test 4: Object.getOwnPropertyDescriptor returns descriptor
const obj4 = { z: 100 };
const desc = Object.getOwnPropertyDescriptor(obj4, 'z');
if (desc && desc.value === 100) {
    console.log("PASS: getOwnPropertyDescriptor returns value");
} else {
    console.log("FAIL: getOwnPropertyDescriptor doesn't return value");
}

// Test 5: getOwnPropertyDescriptor descriptor has expected shape
if (desc && desc.writable === true && desc.enumerable === true && desc.configurable === true) {
    console.log("PASS: Descriptor has expected flags");
} else {
    console.log("FAIL: Descriptor missing expected flags");
}

// Test 6: getOwnPropertyDescriptor for non-existent property
const obj5 = { foo: 'bar' };
const noDesc = Object.getOwnPropertyDescriptor(obj5, 'nonexistent');
if (noDesc === undefined || noDesc === null) {
    console.log("PASS: getOwnPropertyDescriptor returns undefined for missing prop");
} else {
    console.log("FAIL: getOwnPropertyDescriptor returns something for missing prop");
}
