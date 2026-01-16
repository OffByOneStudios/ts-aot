// Test util.types.isMap() distinguishes Map from plain objects
import * as util from 'util';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== util.types.isMap() Tests ===");

    // ============================================================================
    // Test 1: isMap returns true for Map
    // ============================================================================
    {
        console.log("\nTest 1: isMap with new Map()...");
        const map = new Map();
        const result = util.types.isMap(map);
        console.log("isMap(new Map()): " + result);

        if (result === true) {
            console.log("PASS: isMap returns true for Map");
            passed++;
        } else {
            console.log("FAIL: isMap should return true for Map");
            failed++;
        }
    }

    // ============================================================================
    // Test 2: isMap returns false for plain object
    // ============================================================================
    {
        console.log("\nTest 2: isMap with plain object {}...");
        const obj = {};
        const result = util.types.isMap(obj);
        console.log("isMap({}): " + result);

        if (result === false) {
            console.log("PASS: isMap returns false for plain object");
            passed++;
        } else {
            console.log("FAIL: isMap should return false for plain object");
            failed++;
        }
    }

    // ============================================================================
    // Test 3: isMap returns false for object with properties
    // ============================================================================
    {
        console.log("\nTest 3: isMap with object { key: 'value' }...");
        const obj = { key: 'value' };
        const result = util.types.isMap(obj);
        console.log("isMap({ key: 'value' }): " + result);

        if (result === false) {
            console.log("PASS: isMap returns false for object with properties");
            passed++;
        } else {
            console.log("FAIL: isMap should return false for object with properties");
            failed++;
        }
    }

    // ============================================================================
    // Test 4: isMap returns true for Map with entries
    // ============================================================================
    {
        console.log("\nTest 4: isMap with Map containing entries...");
        const map = new Map();
        map.set('key', 'value');
        const result = util.types.isMap(map);
        console.log("isMap(map with entries): " + result);

        if (result === true) {
            console.log("PASS: isMap returns true for Map with entries");
            passed++;
        } else {
            console.log("FAIL: isMap should return true for Map with entries");
            failed++;
        }
    }

    // ============================================================================
    // Test 5: isSet still works
    // ============================================================================
    {
        console.log("\nTest 5: isSet with new Set()...");
        const set = new Set();
        const result = util.types.isSet(set);
        console.log("isSet(new Set()): " + result);

        if (result === true) {
            console.log("PASS: isSet returns true for Set");
            passed++;
        } else {
            console.log("FAIL: isSet should return true for Set");
            failed++;
        }
    }

    // ============================================================================
    // Summary
    // ============================================================================
    console.log("");
    console.log("=== util.types.isMap() Results ===");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);
    console.log("Total: " + (passed + failed));

    return failed;
}
