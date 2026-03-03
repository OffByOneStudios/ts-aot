// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Switch statement with cases, default, and fallthrough
// CHECK: define
// OUTPUT: two
// OUTPUT: other
// OUTPUT: three-fall
// OUTPUT: four-fall

var x = 2;
switch (x) {
    case 1:
        console.log("one");
        break;
    case 2:
        console.log("two");
        break;
    case 3:
        console.log("three");
        break;
    default:
        console.log("other");
}

var y = 99;
switch (y) {
    case 1:
        console.log("one");
        break;
    default:
        console.log("other");
}

// Test fallthrough (no break)
var z = 3;
switch (z) {
    case 1:
        console.log("one-fall");
    case 2:
        console.log("two-fall");
        break;
    case 3:
        console.log("three-fall");
    case 4:
        console.log("four-fall");
        break;
    case 5:
        console.log("five-fall");
        break;
}
