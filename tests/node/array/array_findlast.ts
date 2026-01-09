// Test Array.prototype.findLast() and findLastIndex() methods

function user_main(): number {
    let passed = 0;
    let failed = 0;

    const numbers: number[] = [1, 2, 3, 4, 5, 4, 3, 2, 1];

    // Test 1: findLast - basic usage
    const lastEven = numbers.findLast((x: number) => x % 2 === 0);
    if (lastEven === 2) {
        console.log("PASS: findLast returns last matching element (2)");
        passed++;
    } else {
        console.log("FAIL: findLast expected 2, got " + lastEven);
        failed++;
    }

    // Test 2: findLast - returns last match, not first
    const lastThree = numbers.findLast((x: number) => x === 3);
    if (lastThree === 3) {
        console.log("PASS: findLast returns last 3 (index 6)");
        passed++;
    } else {
        console.log("FAIL: findLast expected 3, got " + lastThree);
        failed++;
    }

    // Test 3: findLast - no match (typed arrays coerce undefined to 0)
    // Note: With number[] type, undefined is coerced to 0 during unboxing
    const notFound = numbers.findLast((x: number) => x > 10);
    if (notFound === 0) {
        console.log("PASS: findLast returns 0 when not found (number coercion)");
        passed++;
    } else {
        console.log("FAIL: findLast expected 0 (coerced), got " + notFound);
        failed++;
    }

    // Test 4: findLastIndex - basic usage
    const lastEvenIndex = numbers.findLastIndex((x: number) => x % 2 === 0);
    if (lastEvenIndex === 7) {
        console.log("PASS: findLastIndex returns last matching index (7)");
        passed++;
    } else {
        console.log("FAIL: findLastIndex expected 7, got " + lastEvenIndex);
        failed++;
    }

    // Test 5: findLastIndex - returns last index, not first
    const lastThreeIndex = numbers.findLastIndex((x: number) => x === 3);
    if (lastThreeIndex === 6) {
        console.log("PASS: findLastIndex returns index 6 for last 3");
        passed++;
    } else {
        console.log("FAIL: findLastIndex expected 6, got " + lastThreeIndex);
        failed++;
    }

    // Test 6: findLastIndex - no match returns -1
    const notFoundIndex = numbers.findLastIndex((x: number) => x > 10);
    if (notFoundIndex === -1) {
        console.log("PASS: findLastIndex returns -1 when not found");
        passed++;
    } else {
        console.log("FAIL: findLastIndex should return -1");
        failed++;
    }

    // Test 7: findLast on empty array (typed arrays coerce undefined to 0)
    const emptyArr: number[] = [];
    const emptyResult = emptyArr.findLast((x: number) => x > 0);
    if (emptyResult === 0) {
        console.log("PASS: findLast on empty array returns 0 (number coercion)");
        passed++;
    } else {
        console.log("FAIL: findLast on empty array expected 0 (coerced), got " + emptyResult);
        failed++;
    }

    // Test 8: findLastIndex on empty array
    const emptyIndexResult = emptyArr.findLastIndex((x: number) => x > 0);
    if (emptyIndexResult === -1) {
        console.log("PASS: findLastIndex on empty array returns -1");
        passed++;
    } else {
        console.log("FAIL: findLastIndex on empty array should return -1");
        failed++;
    }

    // Test 9: findLast - callback receives (element, index, array)
    let capturedIndex = -1;
    numbers.findLast((x: number, i: number) => {
        if (x === 5) capturedIndex = i;
        return x === 5;
    });
    if (capturedIndex === 4) {
        console.log("PASS: findLast callback receives correct index (4)");
        passed++;
    } else {
        console.log("FAIL: findLast expected callback index 4, got " + capturedIndex);
        failed++;
    }

    // Test 10: findLast finds first element when it's the only match at end
    const findFirst = numbers.findLast((x: number) => x === 5);
    if (findFirst === 5) {
        console.log("PASS: findLast finds unique element (5)");
        passed++;
    } else {
        console.log("FAIL: findLast expected 5, got " + findFirst);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
