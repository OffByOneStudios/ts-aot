// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function makeCounter() {
    var count = 0;
    return function() {
        count = count + 1;
        return count;
    };
}
var counter = makeCounter();
console.log(counter());
console.log(counter());
console.log(counter());
