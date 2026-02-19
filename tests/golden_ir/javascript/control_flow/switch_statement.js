// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: two
// OUTPUT: other

var x = 2;
if (x === 1) {
    console.log("one");
} else if (x === 2) {
    console.log("two");
} else if (x === 3) {
    console.log("three");
} else {
    console.log("other");
}

var y = 99;
if (y === 1) {
    console.log("one");
} else {
    console.log("other");
}
