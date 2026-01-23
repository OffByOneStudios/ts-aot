// Test: Index signatures in classes
// RUN: %ts-aot %s -o %t.exe && %t.exe
// OUTPUT: Testing index signatures...
// OUTPUT: Set key1=value1
// OUTPUT: Got key1=value1
// OUTPUT: Set key2=42
// OUTPUT: Got key2=42
// OUTPUT: Set another=hello world
// OUTPUT: Got another=hello world
// OUTPUT: Index signatures working!

class StringMap {
    [key: string]: string;
}

class NumberMap {
    [key: string]: number;
}

function user_main(): number {
    console.log("Testing index signatures...");

    // Test string-value index signature
    const strMap = new StringMap();
    strMap["key1"] = "value1";
    console.log("Set key1=value1");
    const v1 = strMap["key1"];
    console.log("Got key1=" + v1);

    // Test number-value index signature
    const numMap = new NumberMap();
    numMap["key2"] = 42;
    console.log("Set key2=42");
    const v2 = numMap["key2"];
    console.log("Got key2=" + v2);

    // Test multiple values
    strMap["another"] = "hello world";
    console.log("Set another=hello world");
    console.log("Got another=" + strMap["another"]);

    console.log("Index signatures working!");
    return 0;
}
