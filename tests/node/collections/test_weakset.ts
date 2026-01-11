// Test WeakSet ES6 collection

const ws = new WeakSet();

// Test values
const obj1 = { name: "obj1" };
const obj2 = { name: "obj2" };
const obj3 = { name: "obj3" };

// Test add
ws.add(obj1);
ws.add(obj2);
ws.add(obj3);

// Test has
if (ws.has(obj1)) {
    console.log("PASS: has(obj1) is true");
} else {
    console.log("FAIL: has(obj1) is false");
}

if (ws.has(obj2)) {
    console.log("PASS: has(obj2) is true");
} else {
    console.log("FAIL: has(obj2) is false");
}

if (ws.has(obj3)) {
    console.log("PASS: has(obj3) is true");
} else {
    console.log("FAIL: has(obj3) is false");
}

// Test non-existent object
const obj4 = { name: "obj4" };
if (!ws.has(obj4)) {
    console.log("PASS: has(obj4) is false for non-existent object");
} else {
    console.log("FAIL: has(obj4) should be false");
}

// Test delete
const deleted = ws.delete(obj2);
if (deleted) {
    console.log("PASS: delete(obj2) returned true");
} else {
    console.log("FAIL: delete(obj2) should return true");
}

if (!ws.has(obj2)) {
    console.log("PASS: has(obj2) is false after delete");
} else {
    console.log("FAIL: has(obj2) should be false after delete");
}

// Test add chaining
ws.add(obj1).add(obj4);
if (ws.has(obj4)) {
    console.log("PASS: add chaining works");
} else {
    console.log("FAIL: add chaining failed");
}

// Test adding same object twice (should be idempotent)
ws.add(obj1);
if (ws.has(obj1)) {
    console.log("PASS: adding same object twice works");
} else {
    console.log("FAIL: adding same object twice failed");
}

console.log("WeakSet tests complete");

function user_main(): number {
    return 0;
}
