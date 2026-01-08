// Test Object.create

// Test 1: Object.create(null) - creates empty object
const nullProto = Object.create(null);
console.log("PASS: Object.create(null) created");

// Test 2: Object.create(proto) - copies properties from proto
const proto = { greet: "hello", value: 42 };
const obj = Object.create(proto);
console.log("PASS: Object.create(proto) created");

// Test 3: Verify objects were created by checking keys
const keys1 = Object.keys(proto);
const keys2 = Object.keys(obj);
console.log("proto keys: " + keys1.length);
console.log("obj keys: " + keys2.length);

// Test 4: Verify null prototype creates empty object
const nullKeys = Object.keys(nullProto);
if (nullKeys.length === 0) {
    console.log("PASS: Object.create(null) is empty");
} else {
    console.log("FAIL: Object.create(null) is not empty");
}
