// Test Array.prototype.fill()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Fill entire array with a value
    const arr1 = [1, 2, 3, 4, 5];
    arr1.fill(0);
    if (arr1.length === 5 && arr1[0] === 0 && arr1[1] === 0 && arr1[2] === 0 && arr1[3] === 0 && arr1[4] === 0) {
        console.log("PASS: arr.fill(0) fills entire array");
        passed++;
    } else {
        console.log("FAIL: arr.fill(0) - got " + arr1.join(","));
        failed++;
    }

    // Test 2: Fill with start index
    const arr2 = [1, 2, 3, 4, 5];
    arr2.fill(0, 2);
    if (arr2[0] === 1 && arr2[1] === 2 && arr2[2] === 0 && arr2[3] === 0 && arr2[4] === 0) {
        console.log("PASS: arr.fill(0, 2) fills from index 2");
        passed++;
    } else {
        console.log("FAIL: arr.fill(0, 2) - got " + arr2.join(","));
        failed++;
    }

    // Test 3: Fill with start and end index
    const arr3 = [1, 2, 3, 4, 5];
    arr3.fill(0, 1, 3);
    if (arr3[0] === 1 && arr3[1] === 0 && arr3[2] === 0 && arr3[3] === 4 && arr3[4] === 5) {
        console.log("PASS: arr.fill(0, 1, 3) fills indices 1-2");
        passed++;
    } else {
        console.log("FAIL: arr.fill(0, 1, 3) - got " + arr3.join(","));
        failed++;
    }

    // Test 4: Fill with negative start index
    const arr4 = [1, 2, 3, 4, 5];
    arr4.fill(0, -2);
    if (arr4[0] === 1 && arr4[1] === 2 && arr4[2] === 3 && arr4[3] === 0 && arr4[4] === 0) {
        console.log("PASS: arr.fill(0, -2) fills last 2 elements");
        passed++;
    } else {
        console.log("FAIL: arr.fill(0, -2) - got " + arr4.join(","));
        failed++;
    }

    // Test 5: Fill with negative end index
    const arr5 = [1, 2, 3, 4, 5];
    arr5.fill(0, 1, -1);
    if (arr5[0] === 1 && arr5[1] === 0 && arr5[2] === 0 && arr5[3] === 0 && arr5[4] === 5) {
        console.log("PASS: arr.fill(0, 1, -1) fills indices 1-3");
        passed++;
    } else {
        console.log("FAIL: arr.fill(0, 1, -1) - got " + arr5.join(","));
        failed++;
    }

    // Test 6: Fill empty array does nothing
    const arr6: number[] = [];
    arr6.fill(0);
    if (arr6.length === 0) {
        console.log("PASS: fill() on empty array");
        passed++;
    } else {
        console.log("FAIL: fill() on empty array");
        failed++;
    }

    // Test 7: Start >= length does nothing
    const arr7 = [1, 2, 3];
    arr7.fill(0, 10);
    if (arr7[0] === 1 && arr7[1] === 2 && arr7[2] === 3) {
        console.log("PASS: fill() with start >= length does nothing");
        passed++;
    } else {
        console.log("FAIL: fill() with start >= length");
        failed++;
    }

    // Test 8: Start >= end does nothing
    const arr8 = [1, 2, 3];
    arr8.fill(0, 2, 1);
    if (arr8[0] === 1 && arr8[1] === 2 && arr8[2] === 3) {
        console.log("PASS: fill() with start >= end does nothing");
        passed++;
    } else {
        console.log("FAIL: fill() with start >= end");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
