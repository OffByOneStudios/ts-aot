// Test: For loop with counter
for (let i = 0; i < 3; i++) {
    console.log(i);
}

// CHECK: alloca
// CHECK: icmp slt
// CHECK: br i1
// CHECK: add i64

// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2
