// Test that sort() and reverse() return the array type (not void)
// This is a regression test for a bug where sort() was typed as returning void,
// which caused compiler crashes when the result was passed to another function.

function helper(arr: number[]): number {
    return arr[0];
}

function sum(arr: number[]): number {
    let total = 0;
    for (const n of arr) {
        total += n;
    }
    return total;
}

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: sort() with callback - result passed to function
    const arr1: number[] = [3, 1, 2];
    const sorted1 = arr1.sort((a, b) => a - b);
    const first1 = helper(sorted1);
    if (first1 === 1) {
        console.log("PASS: sort() with callback result passed to function");
        passed++;
    } else {
        console.log("FAIL: sort() with callback expected 1, got " + first1);
        failed++;
    }

    // Test 2: sort() without callback - result passed to function
    const arr2: number[] = [3, 1, 2];
    const sorted2 = arr2.sort();
    const first2 = helper(sorted2);
    if (first2 === 1) {
        console.log("PASS: sort() without callback result passed to function");
        passed++;
    } else {
        console.log("FAIL: sort() without callback expected 1, got " + first2);
        failed++;
    }

    // Test 3: reverse() - result passed to function
    const arr3: number[] = [1, 2, 3];
    const reversed = arr3.reverse();
    const first3 = helper(reversed);
    if (first3 === 3) {
        console.log("PASS: reverse() result passed to function");
        passed++;
    } else {
        console.log("FAIL: reverse() expected 3, got " + first3);
        failed++;
    }

    // Test 4: sort() chaining with other methods
    const arr4: number[] = [3, 1, 2];
    const chained = arr4.sort((a, b) => a - b).join(",");
    if (chained === "1,2,3") {
        console.log("PASS: sort().join() chaining works");
        passed++;
    } else {
        console.log("FAIL: sort().join() expected '1,2,3', got " + chained);
        failed++;
    }

    // Test 5: reverse() chaining with other methods
    const arr5: number[] = [1, 2, 3];
    const chainedRev = arr5.reverse().join(",");
    if (chainedRev === "3,2,1") {
        console.log("PASS: reverse().join() chaining works");
        passed++;
    } else {
        console.log("FAIL: reverse().join() expected '3,2,1', got " + chainedRev);
        failed++;
    }

    // Test 6: sort().reverse() chaining
    const arr6: number[] = [2, 3, 1];
    const sortedReversed = arr6.sort((a, b) => a - b).reverse();
    const first6 = helper(sortedReversed);
    if (first6 === 3) {
        console.log("PASS: sort().reverse() chaining works");
        passed++;
    } else {
        console.log("FAIL: sort().reverse() expected 3, got " + first6);
        failed++;
    }

    // Test 7: sort() result used in calculations
    const arr7: number[] = [5, 3, 7, 1];
    const sorted7 = arr7.sort((a, b) => a - b);
    const total = sum(sorted7);
    if (total === 16) {
        console.log("PASS: sort() result used in sum calculation");
        passed++;
    } else {
        console.log("FAIL: sort() sum expected 16, got " + total);
        failed++;
    }

    // Test 8: Descending sort
    const arr8: number[] = [1, 3, 2];
    const descending = arr8.sort((a, b) => b - a);
    if (descending.join(",") === "3,2,1") {
        console.log("PASS: descending sort works");
        passed++;
    } else {
        console.log("FAIL: descending sort expected '3,2,1', got " + descending.join(","));
        failed++;
    }

    // Test 9: sort() returns same array reference
    const arr9: number[] = [3, 1, 2];
    const sorted9 = arr9.sort();
    const sameRef = (arr9 === sorted9);
    if (sameRef) {
        console.log("PASS: sort() returns same array reference");
        passed++;
    } else {
        console.log("FAIL: sort() should return same array reference");
        failed++;
    }

    // Test 10: reverse() returns same array reference
    const arr10: number[] = [1, 2, 3];
    const reversed10 = arr10.reverse();
    const sameRef10 = (arr10 === reversed10);
    if (sameRef10) {
        console.log("PASS: reverse() returns same array reference");
        passed++;
    } else {
        console.log("FAIL: reverse() should return same array reference");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
