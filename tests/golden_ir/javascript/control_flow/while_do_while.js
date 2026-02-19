// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 5
// OUTPUT: 3

var x = 0;
while (x < 5) {
    x = x + 1;
}
console.log(x);

var y = 0;
do {
    y = y + 1;
} while (y < 3);
console.log(y);
