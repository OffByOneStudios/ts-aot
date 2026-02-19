// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 10

function apply(fn, x) {
    return fn(x);
}
function double(x) {
    return x * 2;
}
console.log(apply(double, 5));
