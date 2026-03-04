// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function* count() {
    yield 1;
    yield 2;
    yield 3;
}

var gen = count();
var r = gen.next();
while (!r.done) {
    console.log(r.value);
    r = gen.next();
}
