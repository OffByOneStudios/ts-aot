// Test util.parseEnv() - parses dotenv file content
import * as util from 'util';

// Use object to avoid closure issues
const stats = { passed: 0, failed: 0 };

function test(name: string, fn: () => boolean): void {
    try {
        if (fn()) {
            console.log("PASS: " + name);
            stats.passed = stats.passed + 1;
        } else {
            console.log("FAIL: " + name);
            stats.failed = stats.failed + 1;
        }
    } catch (e) {
        console.log("FAIL: " + name + " (exception)");
        stats.failed = stats.failed + 1;
    }
}

function user_main(): number {
    console.log("Testing util.parseEnv");
    console.log("=====================");
    console.log("");

    // Test 1: Simple key=value
    test("Simple key=value", () => {
        const content = "FOO=bar";
        const result = util.parseEnv(content);
        return result["FOO"] === "bar";
    });

    // Test 2: Multiple key=value pairs
    test("Multiple pairs", () => {
        const content = "FOO=bar\nBAZ=qux";
        const result = util.parseEnv(content);
        return result["FOO"] === "bar" && result["BAZ"] === "qux";
    });

    // Test 3: Quoted value (double quotes)
    test("Double-quoted value", () => {
        const content = 'NAME="John Doe"';
        const result = util.parseEnv(content);
        return result["NAME"] === "John Doe";
    });

    // Test 4: Quoted value (single quotes)
    test("Single-quoted value", () => {
        const content = "NAME='Jane Doe'";
        const result = util.parseEnv(content);
        return result["NAME"] === "Jane Doe";
    });

    // Test 5: Comment lines
    test("Comment lines are skipped", () => {
        const content = "# This is a comment\nFOO=bar\n# Another comment\nBAZ=qux";
        const result = util.parseEnv(content);
        return result["FOO"] === "bar" && result["BAZ"] === "qux";
    });

    // Test 6: Empty lines
    test("Empty lines are skipped", () => {
        const content = "FOO=bar\n\n\nBAZ=qux";
        const result = util.parseEnv(content);
        return result["FOO"] === "bar" && result["BAZ"] === "qux";
    });

    // Test 7: Export prefix
    test("export prefix is handled", () => {
        const content = "export FOO=bar";
        const result = util.parseEnv(content);
        return result["FOO"] === "bar";
    });

    // Test 8: Inline comments (unquoted values)
    test("Inline comments (unquoted)", () => {
        const content = "FOO=bar # this is a comment";
        const result = util.parseEnv(content);
        return result["FOO"] === "bar";
    });

    // Test 9: Escape sequences in double quotes
    test("Escape sequences in double quotes", () => {
        const content = 'MSG="Hello\\nWorld"';
        const result = util.parseEnv(content);
        return result["MSG"] === "Hello\nWorld";
    });

    // Test 10: Whitespace around = is trimmed
    test("Whitespace around = is handled", () => {
        const content = "FOO = bar";
        const result = util.parseEnv(content);
        return result["FOO"] === "bar";
    });

    // Test 11: Empty value
    test("Empty value", () => {
        const content = "EMPTY=";
        const result = util.parseEnv(content);
        return result["EMPTY"] === "";
    });

    // Test 12: Underscores in key names
    test("Underscores in key names", () => {
        const content = "MY_VAR_NAME=value";
        const result = util.parseEnv(content);
        return result["MY_VAR_NAME"] === "value";
    });

    console.log("");
    console.log("Results: " + stats.passed + "/" + (stats.passed + stats.failed) + " passed");
    return stats.failed > 0 ? 1 : 0;
}
