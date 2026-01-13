// Test expressions as default parameter values (ES2015)

function getDefault(): number {
    console.log("getDefault called");
    return 42;
}

const globalValue = 100;

function test1(a: number = 1 + 2) {
    console.log(a);
}

function test2(a: number = getDefault()) {
    console.log(a);
}

function test3(a: number, b: number = a * 2) {
    console.log(b);
}

function test4(a: number = globalValue) {
    console.log(a);
}

function test5(arr: number[] = [1, 2, 3]) {
    console.log(arr.length);
}

function user_main(): number {
    console.log("=== Default Expression Test ===");

    // Test 1: Simple arithmetic expression
    console.log("Test 1: Arithmetic expression");
    test1();      // 3
    test1(10);    // 10

    // Test 2: Function call as default
    console.log("Test 2: Function call default");
    test2();      // getDefault called, 42
    test2(100);   // 100 (no call)

    // Test 3: Using previous parameter
    console.log("Test 3: Previous param reference");
    test3(5);     // 10 (5 * 2)

    // Test 4: Global variable as default
    console.log("Test 4: Global variable");
    test4();      // 100

    // Test 5: Array literal as default
    console.log("Test 5: Array literal");
    test5();      // 3

    console.log("=== Tests Complete ===");
    return 0;
}
