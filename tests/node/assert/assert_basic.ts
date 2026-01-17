// Test assert module basic functionality
import * as assert from 'assert';

function user_main(): number {
    console.log("Testing assert module...");

    // Test assert.ok
    console.log("Testing assert.ok...");
    assert.ok(true);
    assert.ok(1);
    assert.ok("hello");
    console.log("  assert.ok passed");

    // Test assert.equal (loose equality)
    console.log("Testing assert.equal...");
    assert.equal(1, 1);
    assert.equal("hello", "hello");
    console.log("  assert.equal passed");

    // Test assert.notEqual
    console.log("Testing assert.notEqual...");
    assert.notEqual(1, 2);
    assert.notEqual("hello", "world");
    console.log("  assert.notEqual passed");

    // Test assert.strictEqual
    console.log("Testing assert.strictEqual...");
    assert.strictEqual(1, 1);
    assert.strictEqual("hello", "hello");
    assert.strictEqual(true, true);
    console.log("  assert.strictEqual passed");

    // Test assert.notStrictEqual
    console.log("Testing assert.notStrictEqual...");
    assert.notStrictEqual(1, 2);
    assert.notStrictEqual("1", 1 as any);
    console.log("  assert.notStrictEqual passed");

    // Test assert.deepEqual with arrays
    console.log("Testing assert.deepEqual...");
    assert.deepEqual([1, 2, 3], [1, 2, 3]);
    console.log("  assert.deepEqual passed");

    // Test assert.deepStrictEqual
    console.log("Testing assert.deepStrictEqual...");
    assert.deepStrictEqual([1, 2, 3], [1, 2, 3]);
    console.log("  assert.deepStrictEqual passed");

    // Test assert.notDeepEqual
    console.log("Testing assert.notDeepEqual...");
    assert.notDeepEqual([1, 2], [1, 2, 3]);
    console.log("  assert.notDeepEqual passed");

    // Test assert.ifError
    console.log("Testing assert.ifError...");
    assert.ifError(null);
    assert.ifError(undefined);
    assert.ifError(0);
    assert.ifError(false);
    console.log("  assert.ifError passed");

    console.log("All assert tests passed!");
    return 0;
}
