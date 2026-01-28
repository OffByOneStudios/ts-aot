// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_array_push
// OUTPUT: Push object literal: length=1
// OUTPUT: Push object variable: length=1
// OUTPUT: Push with Array<T> syntax: length=1
// OUTPUT: Push multiple objects: length=3
// OUTPUT: All tests passed

// Regression test for Array.push with objects
// Issue: Array<T> syntax was being parsed as Any instead of ArrayType
// This caused push to use dynamic method call instead of optimized ts_array_push

function user_main(): number {
    let passed = true;

    // Test 1: Push object literal directly
    const arr1: { name: string }[] = [];
    arr1.push({ name: "test1" });
    console.log("Push object literal: length=" + arr1.length);
    if (arr1.length !== 1) passed = false;

    // Test 2: Push object variable (the original failing case)
    const obj = { name: "test2" };
    const arr2: Array<{ name: string }> = [];
    arr2.push(obj);
    console.log("Push object variable: length=" + arr2.length);
    if (arr2.length !== 1) passed = false;

    // Test 3: Array<T> syntax with interface-like type
    interface Person {
        name: string;
        age: number;
    }
    const arr3: Array<Person> = [];
    arr3.push({ name: "Alice", age: 30 });
    console.log("Push with Array<T> syntax: length=" + arr3.length);
    if (arr3.length !== 1) passed = false;

    // Test 4: Multiple pushes
    const arr4: Array<{ id: number }> = [];
    arr4.push({ id: 1 });
    arr4.push({ id: 2 });
    arr4.push({ id: 3 });
    console.log("Push multiple objects: length=" + arr4.length);
    if (arr4.length !== 3) passed = false;

    if (passed) {
        console.log("All tests passed");
    } else {
        console.log("FAILED: Some tests did not pass");
    }

    return passed ? 0 : 1;
}
