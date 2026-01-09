// Test ES2023 "change array by copy" methods

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: toReversed - basic usage
    const arr1: number[] = [1, 2, 3, 4, 5];
    const reversed = arr1.toReversed();
    if (reversed.join(",") === "5,4,3,2,1") {
        console.log("PASS: toReversed returns reversed copy");
        passed++;
    } else {
        console.log("FAIL: toReversed expected 5,4,3,2,1, got " + reversed.join(","));
        failed++;
    }

    // Test 2: toReversed - original unchanged
    if (arr1.join(",") === "1,2,3,4,5") {
        console.log("PASS: toReversed does not mutate original");
        passed++;
    } else {
        console.log("FAIL: toReversed mutated original: " + arr1.join(","));
        failed++;
    }

    // Test 3: toSorted - basic usage
    const arr2: number[] = [3, 1, 4, 1, 5, 9, 2, 6];
    const sorted = arr2.toSorted();
    if (sorted.join(",") === "1,1,2,3,4,5,6,9") {
        console.log("PASS: toSorted returns sorted copy");
        passed++;
    } else {
        console.log("FAIL: toSorted expected 1,1,2,3,4,5,6,9, got " + sorted.join(","));
        failed++;
    }

    // Test 4: toSorted - original unchanged
    if (arr2.join(",") === "3,1,4,1,5,9,2,6") {
        console.log("PASS: toSorted does not mutate original");
        passed++;
    } else {
        console.log("FAIL: toSorted mutated original: " + arr2.join(","));
        failed++;
    }

    // Test 5: toSpliced - remove elements
    const arr3: number[] = [1, 2, 3, 4, 5];
    const spliced1 = arr3.toSpliced(1, 2);  // Remove 2 elements starting at index 1
    if (spliced1.join(",") === "1,4,5") {
        console.log("PASS: toSpliced removes elements correctly");
        passed++;
    } else {
        console.log("FAIL: toSpliced expected 1,4,5, got " + spliced1.join(","));
        failed++;
    }

    // Test 6: toSpliced - original unchanged
    if (arr3.join(",") === "1,2,3,4,5") {
        console.log("PASS: toSpliced does not mutate original");
        passed++;
    } else {
        console.log("FAIL: toSpliced mutated original: " + arr3.join(","));
        failed++;
    }

    // Test 7: toSpliced - insert elements
    const arr4: number[] = [1, 2, 5];
    const spliced2 = arr4.toSpliced(2, 0, 3, 4);  // Insert 3,4 at index 2
    if (spliced2.join(",") === "1,2,3,4,5") {
        console.log("PASS: toSpliced inserts elements correctly");
        passed++;
    } else {
        console.log("FAIL: toSpliced expected 1,2,3,4,5, got " + spliced2.join(","));
        failed++;
    }

    // Test 8: with - replace element
    const arr5: number[] = [1, 2, 3, 4, 5];
    const withResult = arr5.with(2, 99);  // Replace index 2 with 99
    if (withResult.join(",") === "1,2,99,4,5") {
        console.log("PASS: with replaces element correctly");
        passed++;
    } else {
        console.log("FAIL: with expected 1,2,99,4,5, got " + withResult.join(","));
        failed++;
    }

    // Test 9: with - original unchanged
    if (arr5.join(",") === "1,2,3,4,5") {
        console.log("PASS: with does not mutate original");
        passed++;
    } else {
        console.log("FAIL: with mutated original: " + arr5.join(","));
        failed++;
    }

    // Test 10: with - negative index
    const arr6: number[] = [1, 2, 3, 4, 5];
    const withNeg = arr6.with(-1, 99);  // Replace last element
    if (withNeg.join(",") === "1,2,3,4,99") {
        console.log("PASS: with handles negative index");
        passed++;
    } else {
        console.log("FAIL: with expected 1,2,3,4,99, got " + withNeg.join(","));
        failed++;
    }

    // Test 11: toReversed - empty array
    const empty: number[] = [];
    const reversedEmpty = empty.toReversed();
    if (reversedEmpty.length === 0) {
        console.log("PASS: toReversed handles empty array");
        passed++;
    } else {
        console.log("FAIL: toReversed empty array should have length 0");
        failed++;
    }

    // Test 12: toSorted - already sorted
    const arr7: number[] = [1, 2, 3];
    const sortedAlready = arr7.toSorted();
    if (sortedAlready.join(",") === "1,2,3") {
        console.log("PASS: toSorted works on already sorted array");
        passed++;
    } else {
        console.log("FAIL: toSorted expected 1,2,3, got " + sortedAlready.join(","));
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
