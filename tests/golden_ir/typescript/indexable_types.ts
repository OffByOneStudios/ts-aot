// Test: Indexable types (interfaces with index signatures)
// RUN: %ts-aot %s -o %t.exe && %t.exe
// OUTPUT: Testing indexable types...
// OUTPUT: Test 1: StringMap
// OUTPUT: Set key1=value1
// OUTPUT: Got key1=value1
// OUTPUT: PASS
// OUTPUT: Test 2: NumberMap
// OUTPUT: Set count=42
// OUTPUT: Got count=42
// OUTPUT: PASS
// OUTPUT: Test 3: MixedMap
// OUTPUT: name=test
// OUTPUT: dynamic=hello
// OUTPUT: PASS
// OUTPUT: All indexable types tests passed!

interface StringMap {
    [key: string]: string;
}

interface NumberMap {
    [key: string]: number;
}

interface MixedMap {
    [key: string]: string | number;
    name: string;  // Known property with index signature
}

function user_main(): number {
    console.log("Testing indexable types...");

    // Test 1: Object literal as StringMap
    const strMap: StringMap = {};
    strMap["key1"] = "value1";
    console.log("Test 1: StringMap");
    console.log("Set key1=value1");
    const v1 = strMap["key1"];
    console.log("Got key1=" + v1);
    if (v1 === "value1") {
        console.log("PASS");
    } else {
        console.log("FAIL: expected value1, got " + v1);
        return 1;
    }

    // Test 2: Object literal as NumberMap
    const numMap: NumberMap = {};
    numMap["count"] = 42;
    console.log("Test 2: NumberMap");
    console.log("Set count=42");
    const v2 = numMap["count"];
    console.log("Got count=" + v2);
    if (v2 === 42) {
        console.log("PASS");
    } else {
        console.log("FAIL: expected 42, got " + v2);
        return 1;
    }

    // Test 3: Interface with known property + index signature
    const mixed: MixedMap = { name: "test" };
    mixed["dynamic"] = "hello";
    console.log("Test 3: MixedMap");
    console.log("name=" + mixed.name);
    console.log("dynamic=" + mixed["dynamic"]);
    if (mixed.name === "test" && mixed["dynamic"] === "hello") {
        console.log("PASS");
    } else {
        console.log("FAIL");
        return 1;
    }

    console.log("All indexable types tests passed!");
    return 0;
}
