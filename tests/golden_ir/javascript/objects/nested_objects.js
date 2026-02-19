// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello
// OUTPUT: 42

var obj = {
    inner: {
        value: "hello"
    },
    count: 42
};
console.log(obj.inner.value);
console.log(obj.count);
