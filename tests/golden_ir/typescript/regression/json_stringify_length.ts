// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: length: 17
// OUTPUT: PASS

function user_main(): number {
    const obj = { value: 3.14159 };
    const json = JSON.stringify(obj);

    // This should work correctly now that JSON.stringify
    // returns TypeKind::String (not Any)
    const len = json.length;
    console.log("length: " + len);

    // Expected: {"value":3.14159} is 17 characters
    if (len === 17) {
        console.log("PASS");
    } else {
        console.log("FAIL: expected 17, got " + len);
    }

    return 0;
}
