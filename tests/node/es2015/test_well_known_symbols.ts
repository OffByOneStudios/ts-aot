// Test well-known symbols

function user_main(): number {
    console.log("Testing well-known symbols...");
    let passed = 0;
    let failed = 0;

    // Test 1: Symbol.iterator
    console.log("\n1. Symbol.iterator:");
    const iterKey = Symbol.iterator;
    console.log("Symbol.iterator = " + iterKey);
    if (iterKey === "[Symbol.iterator]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.iterator]");
        failed++;
    }

    // Test 2: Symbol.asyncIterator
    console.log("\n2. Symbol.asyncIterator:");
    const asyncIterKey = Symbol.asyncIterator;
    console.log("Symbol.asyncIterator = " + asyncIterKey);
    if (asyncIterKey === "[Symbol.asyncIterator]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.asyncIterator]");
        failed++;
    }

    // Test 3: Symbol.toStringTag
    console.log("\n3. Symbol.toStringTag:");
    const toStringTagKey = Symbol.toStringTag;
    console.log("Symbol.toStringTag = " + toStringTagKey);
    if (toStringTagKey === "[Symbol.toStringTag]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.toStringTag]");
        failed++;
    }

    // Test 4: Symbol.toPrimitive
    console.log("\n4. Symbol.toPrimitive:");
    const toPrimitiveKey = Symbol.toPrimitive;
    console.log("Symbol.toPrimitive = " + toPrimitiveKey);
    if (toPrimitiveKey === "[Symbol.toPrimitive]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.toPrimitive]");
        failed++;
    }

    // Test 5: Symbol.hasInstance
    console.log("\n5. Symbol.hasInstance:");
    const hasInstanceKey = Symbol.hasInstance;
    console.log("Symbol.hasInstance = " + hasInstanceKey);
    if (hasInstanceKey === "[Symbol.hasInstance]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.hasInstance]");
        failed++;
    }

    // Test 6: Symbol.isConcatSpreadable
    console.log("\n6. Symbol.isConcatSpreadable:");
    const isConcatSpreadableKey = Symbol.isConcatSpreadable;
    console.log("Symbol.isConcatSpreadable = " + isConcatSpreadableKey);
    if (isConcatSpreadableKey === "[Symbol.isConcatSpreadable]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.isConcatSpreadable]");
        failed++;
    }

    // Test 7: Symbol.match
    console.log("\n7. Symbol.match:");
    const matchKey = Symbol.match;
    console.log("Symbol.match = " + matchKey);
    if (matchKey === "[Symbol.match]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.match]");
        failed++;
    }

    // Test 8: Symbol.replace
    console.log("\n8. Symbol.replace:");
    const replaceKey = Symbol.replace;
    console.log("Symbol.replace = " + replaceKey);
    if (replaceKey === "[Symbol.replace]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.replace]");
        failed++;
    }

    // Test 9: Symbol.search
    console.log("\n9. Symbol.search:");
    const searchKey = Symbol.search;
    console.log("Symbol.search = " + searchKey);
    if (searchKey === "[Symbol.search]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.search]");
        failed++;
    }

    // Test 10: Symbol.split
    console.log("\n10. Symbol.split:");
    const splitKey = Symbol.split;
    console.log("Symbol.split = " + splitKey);
    if (splitKey === "[Symbol.split]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.split]");
        failed++;
    }

    // Test 11: Symbol.species
    console.log("\n11. Symbol.species:");
    const speciesKey = Symbol.species;
    console.log("Symbol.species = " + speciesKey);
    if (speciesKey === "[Symbol.species]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.species]");
        failed++;
    }

    // Test 12: Symbol.unscopables
    console.log("\n12. Symbol.unscopables:");
    const unscopablesKey = Symbol.unscopables;
    console.log("Symbol.unscopables = " + unscopablesKey);
    if (unscopablesKey === "[Symbol.unscopables]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.unscopables]");
        failed++;
    }

    // Test 13: Symbol.matchAll (ES2020)
    console.log("\n13. Symbol.matchAll:");
    const matchAllKey = Symbol.matchAll;
    console.log("Symbol.matchAll = " + matchAllKey);
    if (matchAllKey === "[Symbol.matchAll]") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected [Symbol.matchAll]");
        failed++;
    }

    // Test 14: Using symbols as property keys
    console.log("\n14. Using Symbol.toStringTag as property key:");
    const obj: any = {};
    obj[Symbol.toStringTag] = "MyCustomObject";
    const tag = obj[Symbol.toStringTag];
    console.log("obj[Symbol.toStringTag] = " + tag);
    if (tag === "MyCustomObject") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected MyCustomObject");
        failed++;
    }

    // Test 15: Custom iterator using Symbol.iterator (using closure to capture data)
    console.log("\n15. Custom iterable with Symbol.iterator:");
    const iterData: number[] = [100, 200, 300];
    const customIterable: any = {
        "[Symbol.iterator]": function(): any {
            let index = 0;
            return {
                next: function(): any {
                    if (index < iterData.length) {
                        return { value: iterData[index++], done: false };
                    }
                    return { value: undefined, done: true };
                }
            };
        }
    };

    let sum = 0;
    for (const val of customIterable) {
        sum += val;
    }
    console.log("Sum from custom iterator: " + sum);
    if (sum === 600) {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL: expected 600");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All well-known symbol tests passed!");
    }

    return failed;
}
