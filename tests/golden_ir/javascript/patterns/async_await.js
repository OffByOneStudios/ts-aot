// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: start
// OUTPUT: got 42
// OUTPUT: done

async function getValue() {
    return 42;
}

async function user_main() {
    console.log("start");
    var val = await getValue();
    console.log("got " + val);
    console.log("done");
    return 0;
}
