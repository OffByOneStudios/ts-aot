// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: start
// OUTPUT: 42
// OUTPUT: done

// Test async/await with Promise.resolve
async function getValue() {
    return 42;
}

async function user_main() {
    console.log("start");
    var val = await getValue();
    console.log(val);
    console.log("done");
    return 0;
}
