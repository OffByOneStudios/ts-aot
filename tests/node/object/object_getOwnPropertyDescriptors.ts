// Test Object.getOwnPropertyDescriptors (ES2017)

let passed = 0;
let failed = 0;

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("PASS: " + name);
        passed++;
    } else {
        console.log("FAIL: " + name);
        failed++;
    }
}

// Test 1: Basic object with multiple properties
const obj1 = { a: 1, b: "hello", c: true };
const descriptors1 = Object.getOwnPropertyDescriptors(obj1);

test("Returns object with same keys",
    Object.keys(descriptors1).length === 3);

test("Descriptor for 'a' has correct value",
    descriptors1["a"]["value"] === 1);

test("Descriptor for 'b' has correct value",
    descriptors1["b"]["value"] === "hello");

test("Descriptor for 'c' has correct value",
    descriptors1["c"]["value"] === true);

test("Descriptor has writable property",
    descriptors1["a"]["writable"] === true);

test("Descriptor has enumerable property",
    descriptors1["a"]["enumerable"] === true);

test("Descriptor has configurable property",
    descriptors1["a"]["configurable"] === true);

// Test 2: Empty object
const obj2 = {};
const descriptors2 = Object.getOwnPropertyDescriptors(obj2);
test("Empty object returns empty descriptors",
    Object.keys(descriptors2).length === 0);

// Test 3: Object with numeric value
const obj3 = { count: 42 };
const descriptors3 = Object.getOwnPropertyDescriptors(obj3);
test("Numeric value preserved",
    descriptors3["count"]["value"] === 42);

// Test 4: Can use descriptors with Object.defineProperties
const target = {};
const source = { x: 10, y: 20 };
const sourceDescriptors = Object.getOwnPropertyDescriptors(source);
Object.defineProperties(target, sourceDescriptors);
test("Can copy object using descriptors",
    target["x"] === 10 && target["y"] === 20);

// Summary
console.log("---");
console.log("Passed: " + String(passed));
console.log("Failed: " + String(failed));

function user_main(): number {
    return failed;
}
