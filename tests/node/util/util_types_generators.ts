// Test util.types.isGeneratorObject() for generators
import * as util from 'util';

function* simpleGenerator() {
    yield 1;
    yield 2;
    yield 3;
}

function user_main(): number {
    console.log("=== util.types.isGeneratorObject() Tests ===");
    console.log("");
    let failures = 0;

    // Test 1: Generator object created from generator function
    const gen = simpleGenerator();
    const result1 = util.types.isGeneratorObject(gen);
    if (result1 === true) {
        console.log("PASS: Generator object detected as generator");
    } else {
        console.log("FAIL: Expected true for generator object, got: " + result1);
        failures++;
    }

    // Test 2: Plain object should NOT be a generator
    const plainObj = { next: function() { return { value: 1, done: false }; } };
    const result2 = util.types.isGeneratorObject(plainObj);
    if (result2 === false) {
        console.log("PASS: Plain object with next() NOT detected as generator");
    } else {
        console.log("FAIL: Expected false for plain object, got: " + result2);
        failures++;
    }

    // Test 3: Array should NOT be a generator
    const arr = [1, 2, 3];
    const result3 = util.types.isGeneratorObject(arr);
    if (result3 === false) {
        console.log("PASS: Array NOT detected as generator");
    } else {
        console.log("FAIL: Expected false for array, got: " + result3);
        failures++;
    }

    // Test 4: Iterate the generator to make sure it still works
    const gen2 = simpleGenerator();
    let sum = 0;
    for (const val of gen2) {
        sum += val;
    }
    if (sum === 6) {
        console.log("PASS: Generator iteration works correctly (sum = 6)");
    } else {
        console.log("FAIL: Expected sum of 6, got: " + sum);
        failures++;
    }

    console.log("");
    console.log("=== Summary ===");
    if (failures === 0) {
        console.log("All tests passed!");
    } else {
        console.log("Failures: " + failures);
    }

    return failures;
}
