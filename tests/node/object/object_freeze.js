// Test Object.freeze, seal, preventExtensions and related methods

// Test 1: Object.freeze returns the object
const obj1 = { x: 1, y: 2 };
const frozen = Object.freeze(obj1);
if (frozen === obj1) {
    console.log("PASS: Object.freeze returns same object");
} else {
    console.log("FAIL: Object.freeze doesn't return same object");
}

// Test 2: Object.isFrozen returns true for frozen objects
if (Object.isFrozen(frozen)) {
    console.log("PASS: Object.isFrozen returns true");
} else {
    console.log("FAIL: Object.isFrozen returns false");
}

// Test 3: Object.seal returns the object
const obj2 = { a: 1, b: 2 };
const sealed = Object.seal(obj2);
if (sealed === obj2) {
    console.log("PASS: Object.seal returns same object");
} else {
    console.log("FAIL: Object.seal doesn't return same object");
}

// Test 4: Object.isSealed returns true for sealed objects
if (Object.isSealed(sealed)) {
    console.log("PASS: Object.isSealed returns true");
} else {
    console.log("FAIL: Object.isSealed returns false");
}

// Test 5: Object.preventExtensions
const obj3 = { p: 1 };
const nonExt = Object.preventExtensions(obj3);
if (nonExt === obj3) {
    console.log("PASS: Object.preventExtensions returns same object");
} else {
    console.log("FAIL: Object.preventExtensions doesn't return same object");
}

// Test 6: Object.isExtensible returns false for non-extensible objects
if (!Object.isExtensible(nonExt)) {
    console.log("PASS: Object.isExtensible returns false");
} else {
    console.log("FAIL: Object.isExtensible returns true");
}

// Test 7: Normal object is extensible
const obj4 = { z: 1 };
if (Object.isExtensible(obj4)) {
    console.log("PASS: Normal object is extensible");
} else {
    console.log("FAIL: Normal object is not extensible");
}

// Test 8: Frozen object counts as sealed
if (Object.isSealed(frozen)) {
    console.log("PASS: Frozen object is sealed");
} else {
    console.log("FAIL: Frozen object is not sealed");
}
