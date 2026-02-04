// Test: For loop with counter
for (let i = 0; i < 3; i++) {
    console.log(i);
}

// CHECK: alloca
// CHECK: fcmp olt
// CHECK: br i1
// CHECK: fadd

// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2
