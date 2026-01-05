// Test: Switch statement with multiple cases
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

// CHECK: switch i64
// CHECK: br label

// OUTPUT: 20
