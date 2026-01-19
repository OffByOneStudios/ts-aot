// Comprehensive test for Node.js assert module
import * as assert from 'assert';

function user_main(): number {
    console.log("=== Assert Module Comprehensive Test ===\n");

    let passed = 0;
    let total = 0;

    function test(name: string, fn: () => void): void {
        total++;
        fn();
        passed++;
        console.log("PASS: " + name);
    }

    // ===== assert() and assert.ok() =====
    console.log("--- Basic Assertions ---");

    test("assert(true)", () => {
        assert(true);
    });

    test("assert(1)", () => {
        assert(1);
    });

    test("assert('hello')", () => {
        assert("hello");
    });

    test("assert.ok(true)", () => {
        assert.ok(true);
    });

    test("assert.ok({})", () => {
        assert.ok({});
    });

    test("assert.ok([])", () => {
        assert.ok([]);
    });

    // ===== assert.equal() - Loose equality =====
    console.log("\n--- Loose Equality (equal/notEqual) ---");

    test("assert.equal(1, 1)", () => {
        assert.equal(1, 1);
    });

    test("assert.equal('hello', 'hello')", () => {
        assert.equal("hello", "hello");
    });

    test("assert.equal(true, true)", () => {
        assert.equal(true, true);
    });

    test("assert.notEqual(1, 2)", () => {
        assert.notEqual(1, 2);
    });

    test("assert.notEqual('a', 'b')", () => {
        assert.notEqual("a", "b");
    });

    test("assert.notEqual(true, false)", () => {
        assert.notEqual(true, false);
    });

    // ===== assert.strictEqual() - Strict equality =====
    console.log("\n--- Strict Equality (strictEqual/notStrictEqual) ---");

    test("assert.strictEqual(1, 1)", () => {
        assert.strictEqual(1, 1);
    });

    test("assert.strictEqual('test', 'test')", () => {
        assert.strictEqual("test", "test");
    });

    test("assert.strictEqual(null, null)", () => {
        assert.strictEqual(null, null);
    });

    test("assert.notStrictEqual(1, 2)", () => {
        assert.notStrictEqual(1, 2);
    });

    test("assert.notStrictEqual('1', 1 as any)", () => {
        assert.notStrictEqual("1", 1 as any);
    });

    // ===== assert.deepEqual() - Deep equality =====
    console.log("\n--- Deep Equality (deepEqual/notDeepEqual) ---");

    test("assert.deepEqual([1,2,3], [1,2,3])", () => {
        assert.deepEqual([1, 2, 3], [1, 2, 3]);
    });

    test("assert.deepEqual([], [])", () => {
        assert.deepEqual([], []);
    });

    test("assert.notDeepEqual([1,2], [1,2,3])", () => {
        assert.notDeepEqual([1, 2], [1, 2, 3]);
    });

    test("assert.notDeepEqual([1], [2])", () => {
        assert.notDeepEqual([1], [2]);
    });

    // ===== assert.deepStrictEqual() - Deep strict equality =====
    console.log("\n--- Deep Strict Equality (deepStrictEqual/notDeepStrictEqual) ---");

    test("assert.deepStrictEqual([1,2], [1,2])", () => {
        assert.deepStrictEqual([1, 2], [1, 2]);
    });

    test("assert.deepStrictEqual(['a','b'], ['a','b'])", () => {
        assert.deepStrictEqual(["a", "b"], ["a", "b"]);
    });

    test("assert.notDeepStrictEqual([1,2,3], [1,2])", () => {
        assert.notDeepStrictEqual([1, 2, 3], [1, 2]);
    });

    test("assert.notDeepStrictEqual([1], [2])", () => {
        assert.notDeepStrictEqual([1], [2]);
    });

    // ===== assert.match() / assert.doesNotMatch() =====
    console.log("\n--- Regex Matching (match/doesNotMatch) ---");

    test("assert.match('hello world', /world/)", () => {
        assert.match("hello world", /world/);
    });

    test("assert.match('test123', /\\d+/)", () => {
        assert.match("test123", /\d+/);
    });

    test("assert.match('email@test.com', /@/)", () => {
        assert.match("email@test.com", /@/);
    });

    test("assert.doesNotMatch('hello', /xyz/)", () => {
        assert.doesNotMatch("hello", /xyz/);
    });

    test("assert.doesNotMatch('abc', /\\d/)", () => {
        assert.doesNotMatch("abc", /\d/);
    });

    // ===== assert.ifError() =====
    console.log("\n--- ifError (for error-first callbacks) ---");

    test("assert.ifError(null)", () => {
        assert.ifError(null);
    });

    test("assert.ifError(undefined)", () => {
        assert.ifError(undefined);
    });

    test("assert.ifError(0)", () => {
        assert.ifError(0);
    });

    test("assert.ifError(false)", () => {
        assert.ifError(false);
    });

    test("assert.ifError('')", () => {
        assert.ifError("");
    });

    // ===== assert.doesNotThrow() =====
    console.log("\n--- doesNotThrow ---");

    test("assert.doesNotThrow with safe function", () => {
        assert.doesNotThrow(() => {
            const x = 1 + 1;
        });
    });

    test("assert.doesNotThrow with function returning value", () => {
        assert.doesNotThrow(() => {
            return "safe";
        });
    });

    // ===== assert.throws() - stub with warning =====
    console.log("\n--- throws (stub - no try/catch in AOT) ---");
    console.log("Note: assert.throws() cannot verify exceptions in AOT-compiled code");
    // We call it to show the warning, but it doesn't actually test anything
    assert.throws(() => {
        // This would throw in Node.js, but we can't catch it in AOT
    });
    console.log("PASS: assert.throws stub executed (warning displayed)");
    passed++;
    total++;

    // ===== Summary =====
    console.log("\n=== Test Summary ===");
    console.log("Passed: " + passed + "/" + total);

    if (passed === total) {
        console.log("All tests passed!");
        return 0;
    } else {
        console.log("Some tests failed!");
        return 1;
    }
}
