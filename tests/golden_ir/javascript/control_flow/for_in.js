// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: a
// OUTPUT: b

var obj = { a: 1, b: 2 };
for (var key in obj) {
    console.log(key);
}
