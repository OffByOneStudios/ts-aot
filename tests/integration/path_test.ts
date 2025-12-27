import * as path from 'path';

function test(name: string, actual: any, expected: any) {
    if (actual === expected) {
        console.log(`PASS: ${name}`);
    } else {
        console.log(`FAIL: ${name} - expected: ${expected}, actual: ${actual}`);
        process.exit(1);
    }
}

console.log("Testing path module...");

// Properties
// Note: These are platform dependent, but we can check they are strings
test("path.sep is string", typeof path.sep, "string");
test("path.delimiter is string", typeof path.delimiter, "string");

const isWindows = path.sep === '\\';

// join
test("path.join basic", path.join("a", "b", "c"), isWindows ? "a\\b\\c" : "a/b/c");
test("path.join with empty", path.join("a", "", "b"), isWindows ? "a\\b" : "a/b");
test("path.join with dot", path.join("a", ".", "b"), isWindows ? "a\\b" : "a/b");

// resolve
// path.resolve returns absolute path, so we just check it's a string for now
test("path.resolve is string", typeof path.resolve("a", "b"), "string");

// normalize
test("path.normalize basic", path.normalize("a//b/../c"), isWindows ? "a\\c" : "a/c");

// isAbsolute
test("path.isAbsolute false", path.isAbsolute("a/b"), false);
// On windows, C:/ is absolute, on posix / is absolute.
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

// relative
test("path.relative basic", path.relative("/data/orandea/test/aaa", "/data/orandea/impl/bbb"), isWindows ? "..\\..\\impl\\bbb" : "../../impl/bbb");

console.log("All path tests passed!");
