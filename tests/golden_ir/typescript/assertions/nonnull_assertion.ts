// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: hello
// OUTPUT: 42.000000
// OUTPUT: test

function user_main(): number {
    // Test 1: Non-null assertion on nullable value
    const maybeStr: string | null = "hello";
    const str: string = maybeStr!;
    console.log(str);

    // Test 2: Non-null assertion on optional property
    const obj: { value?: number } = { value: 42 };
    const val: number = obj.value!;
    console.log(val);

    // Test 3: Chained non-null assertion
    const nested: { inner?: { data: string } } = { inner: { data: "test" } };
    const data: string = nested.inner!.data;
    console.log(data);

    return 0;
}
