// Test util.inspect function
import * as util from 'util';

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

// Test inspecting primitives
console.log("\n--- Primitive inspection ---");

const undefinedStr = util.inspect(undefined);
test("inspect(undefined) returns 'undefined'", undefinedStr === "undefined");

const boolStr = util.inspect(true);
test("inspect(true) returns 'true'", boolStr === "true");

const numStr = util.inspect(42);
test("inspect(42) returns '42'", numStr.indexOf("42") >= 0);

const strStr = util.inspect("hello");
test("inspect('hello') contains 'hello'", strStr.indexOf("hello") >= 0);

// Test inspecting arrays
console.log("\n--- Array inspection ---");

const arrStr = util.inspect([1, 2, 3]);
test("inspect([1, 2, 3]) contains brackets", arrStr.indexOf("[") >= 0 && arrStr.indexOf("]") >= 0);
test("inspect([1, 2, 3]) contains elements", arrStr.indexOf("1") >= 0 && arrStr.indexOf("2") >= 0);

const emptyArrStr = util.inspect([]);
test("inspect([]) returns '[]'", emptyArrStr === "[]");

// Test inspecting objects
console.log("\n--- Object inspection ---");

const objStr = util.inspect({name: "Alice", age: 30});
test("inspect({name, age}) contains name", objStr.indexOf("name") >= 0 || objStr.indexOf("Alice") >= 0);

const emptyObjStr = util.inspect({});
test("inspect({}) returns object representation", emptyObjStr.indexOf("{") >= 0 && emptyObjStr.indexOf("}") >= 0);

// Test inspecting Date
console.log("\n--- Date inspection ---");

const now = new Date();
const dateStr = util.inspect(now);
test("inspect(Date) returns ISO-like string", dateStr.indexOf("20") >= 0); // Should contain year

// Test inspecting RegExp
console.log("\n--- RegExp inspection ---");

const re = /hello/gi;
const reStr = util.inspect(re);
test("inspect(/hello/gi) contains /hello/", reStr.indexOf("hello") >= 0);
test("inspect(/hello/gi) contains flags", reStr.indexOf("g") >= 0 && reStr.indexOf("i") >= 0);

// Test inspecting Map
console.log("\n--- Map inspection ---");

const map = new Map();
map.set("key1", "value1");
const mapStr = util.inspect(map);
test("inspect(Map) returns Map representation", mapStr.indexOf("Map") >= 0 || mapStr.indexOf("key1") >= 0);

// Test inspecting Set
console.log("\n--- Set inspection ---");

const set = new Set();
set.add(1);
set.add(2);
const setStr = util.inspect(set);
test("inspect(Set) returns Set representation", setStr.indexOf("Set") >= 0 || (setStr.indexOf("1") >= 0 && setStr.indexOf("2") >= 0));

// Summary
console.log("\n=== SUMMARY ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);
console.log("Total: " + (passed + failed));

function user_main(): number {
    return failed;
}
