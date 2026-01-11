// Test WeakMap ES6 collection

const wm = new WeakMap();

// Test keys
const key1 = { name: "key1" };
const key2 = { name: "key2" };
const key3 = { name: "key3" };

// Test set and get
wm.set(key1, "value1");
wm.set(key2, 42);
wm.set(key3, { nested: true });

// Test get
const val1 = wm.get(key1);
if (val1 === "value1") {
    console.log("PASS: get(key1) === 'value1'");
} else {
    console.log("FAIL: get(key1) !== 'value1'");
}

const val2 = wm.get(key2);
if (val2 === 42) {
    console.log("PASS: get(key2) === 42");
} else {
    console.log("FAIL: get(key2) !== 42");
}

// Test has
if (wm.has(key1)) {
    console.log("PASS: has(key1) is true");
} else {
    console.log("FAIL: has(key1) is false");
}

if (wm.has(key2)) {
    console.log("PASS: has(key2) is true");
} else {
    console.log("FAIL: has(key2) is false");
}

// Test non-existent key
const key4 = { name: "key4" };
if (!wm.has(key4)) {
    console.log("PASS: has(key4) is false for non-existent key");
} else {
    console.log("FAIL: has(key4) should be false");
}

const val4 = wm.get(key4);
if (val4 === undefined) {
    console.log("PASS: get(key4) === undefined for non-existent key");
} else {
    console.log("FAIL: get(key4) should be undefined");
}

// Test delete
const deleted = wm.delete(key2);
if (deleted) {
    console.log("PASS: delete(key2) returned true");
} else {
    console.log("FAIL: delete(key2) should return true");
}

if (!wm.has(key2)) {
    console.log("PASS: has(key2) is false after delete");
} else {
    console.log("FAIL: has(key2) should be false after delete");
}

// Test update value
wm.set(key1, "updated");
const updated = wm.get(key1);
if (updated === "updated") {
    console.log("PASS: update value works");
} else {
    console.log("FAIL: update value failed");
}

console.log("WeakMap tests complete");

function user_main(): number {
    return 0;
}
