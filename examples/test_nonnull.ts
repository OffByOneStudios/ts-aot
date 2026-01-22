// Test non-null assertion operator (!)
function user_main(): number {
    // Test 1: Non-null assertion on nullable value
    const maybeStr: string | null = "hello";
    const str: string = maybeStr!;  // Should compile and work
    console.log("str:", str);
    
    // Test 2: Non-null assertion on optional property access
    const obj: { value?: number } = { value: 42 };
    const val: number = obj.value!;
    console.log("val:", val);
    
    // Test 3: Chained non-null assertion  
    const nested: { inner?: { data: string } } = { inner: { data: "test" } };
    const data: string = nested.inner!.data;
    console.log("data:", data);
    
    return 0;
}
