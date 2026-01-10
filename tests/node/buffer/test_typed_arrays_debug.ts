// Debug test for TypedArray - basic length check

function user_main(): number {
    console.log("Creating Int8Array...");
    const i8 = new Int8Array(4);
    console.log("Int8Array created");
    console.log("Checking length...");
    const len = i8.length;
    console.log("Length value:", len);
    return 0;
}
