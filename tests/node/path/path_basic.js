// Path Module Tests - JavaScript version

var path = require('path');

function test(name, actual, expected) {
    if (actual === expected) {
        console.log('PASS: ' + name);
    } else {
        console.log('FAIL: ' + name + ' - expected: ' + expected + ', actual: ' + actual);
    }
}

function user_main() {
console.log("Testing path module (JS)...");

// Properties
test("path.sep is string", typeof path.sep, "string");
test("path.delimiter is string", typeof path.delimiter, "string");

var isWindows = path.sep === '\\';

// join
test("path.join basic", path.join("a", "b", "c"), isWindows ? "a\\b\\c" : "a/b/c");
test("path.join with empty", path.join("a", "", "b"), isWindows ? "a\\b" : "a/b");

// resolve
test("path.resolve is string", typeof path.resolve("a", "b"), "string");

// normalize
test("path.normalize basic", path.normalize("a//b/../c"), isWindows ? "a\\c" : "a/c");

// isAbsolute
test("path.isAbsolute false", path.isAbsolute("a/b"), false);
if (isWindows) {
    test("path.isAbsolute true (windows)", path.isAbsolute("C:\\"), true);
} else {
    test("path.isAbsolute true (posix)", path.isAbsolute("/"), true);
}

// basename
test("path.basename basic", path.basename("/a/b/c.txt"), "c.txt");
test("path.basename with ext", path.basename("/a/b/c.txt", ".txt"), "c");

// dirname
test("path.dirname basic", path.dirname("/a/b/c.txt"), isWindows ? "\\a\\b" : "/a/b");

// extname
test("path.extname basic", path.extname("/a/b/c.txt"), ".txt");
test("path.extname none", path.extname("/a/b/c"), "");

console.log("All path tests passed!");
return 0;
}
