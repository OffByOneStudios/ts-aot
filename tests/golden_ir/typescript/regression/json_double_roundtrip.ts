// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: original: 3.14159
// OUTPUT: parsed: 3.14159
// OUTPUT: roundtrip: PASS

function user_main(): number {
    const original = { value: 3.14159 };
    const json = JSON.stringify(original);
    const parsed = JSON.parse(json);

    console.log("original: " + original.value);
    console.log("parsed: " + parsed.value);

    // Verify round-trip preserves value
    const reparsed = JSON.stringify(parsed);
    console.log("roundtrip: " + (json === reparsed ? "PASS" : "FAIL"));

    return 0;
}
