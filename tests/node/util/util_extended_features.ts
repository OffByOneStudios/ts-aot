// Test util extended features
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

// Test util.getSystemErrorName
console.log("\n--- util.getSystemErrorName ---");

// Test common error codes
const eperm = util.getSystemErrorName(1);
test("getSystemErrorName(1) returns EPERM", eperm === "EPERM");

const enoent = util.getSystemErrorName(2);
test("getSystemErrorName(2) returns ENOENT", enoent === "ENOENT");

const eacces = util.getSystemErrorName(13);
test("getSystemErrorName(13) returns EACCES", eacces === "EACCES");

const einval = util.getSystemErrorName(22);
test("getSystemErrorName(22) returns EINVAL", einval === "EINVAL");

const econnrefused = util.getSystemErrorName(111);
test("getSystemErrorName(111) returns ECONNREFUSED", econnrefused === "ECONNREFUSED");

// Unknown error code should return empty string
const unknown = util.getSystemErrorName(9999);
test("getSystemErrorName(9999) returns empty string", unknown === "");

// Test util.getSystemErrorMap
console.log("\n--- util.getSystemErrorMap ---");

const errorMap = util.getSystemErrorMap();
test("getSystemErrorMap() returns a Map-like object", errorMap !== null && errorMap !== undefined);

// Test util.styleText
console.log("\n--- util.styleText ---");

const boldText = util.styleText("bold", "Hello");
test("styleText('bold', 'Hello') contains escape codes", boldText.indexOf("\x1b[") >= 0);
test("styleText('bold', 'Hello') contains 'Hello'", boldText.indexOf("Hello") >= 0);

const redText = util.styleText("red", "Error");
test("styleText('red', 'Error') contains escape codes", redText.indexOf("\x1b[") >= 0);
test("styleText('red', 'Error') contains 'Error'", redText.indexOf("Error") >= 0);

const greenText = util.styleText("green", "Success");
test("styleText('green', 'Success') contains 'Success'", greenText.indexOf("Success") >= 0);

// Test util.formatWithOptions
console.log("\n--- util.formatWithOptions ---");

const formatted = util.formatWithOptions({}, "Hello %s", "World");
test("formatWithOptions works with string placeholder", formatted.indexOf("Hello") >= 0);

// Test util.format with various placeholders
console.log("\n--- util.format ---");

const formatS = util.format("Name: %s", "Alice");
test("format %s works", formatS.indexOf("Alice") >= 0);

const formatD = util.format("Count: %d", 42);
test("format %d works", formatD.indexOf("42") >= 0);

const formatMultiple = util.format("%s has %d apples", "Bob", 5);
test("format multiple placeholders", formatMultiple.indexOf("Bob") >= 0 && formatMultiple.indexOf("5") >= 0);

// Test util.stripVTControlCharacters
console.log("\n--- util.stripVTControlCharacters ---");

const withCodes = "\x1b[31mRed Text\x1b[0m";
const stripped = util.stripVTControlCharacters(withCodes);
test("stripVTControlCharacters removes ANSI codes", stripped === "Red Text");

const plainText = "Plain Text";
const strippedPlain = util.stripVTControlCharacters(plainText);
test("stripVTControlCharacters keeps plain text unchanged", strippedPlain === "Plain Text");

// Test util.toUSVString
console.log("\n--- util.toUSVString ---");

const validString = "Hello World";
const usvValid = util.toUSVString(validString);
test("toUSVString keeps valid string unchanged", usvValid === "Hello World");

// Summary
console.log("\n=== SUMMARY ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);
console.log("Total: " + (passed + failed));

function user_main(): number {
    return failed;
}
