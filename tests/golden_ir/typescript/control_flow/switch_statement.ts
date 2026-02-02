// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Switch statement with multiple cases
// CHECK-DAG: fcmp {{.*}} oeq
// CHECK-DAG: br i1
// CHECK-DAG: switch.end
// OUTPUT: 20

const x = 2;

switch (x) {
    case 1:
        console.log(10);
        break;
    case 2:
        console.log(20);
        break;
    case 3:
        console.log(30);
        break;
    default:
        console.log(0);
}
