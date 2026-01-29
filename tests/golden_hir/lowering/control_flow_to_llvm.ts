// Test: HIR control flow lowers correctly to LLVM IR
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// Verify HIR conditional branch
// HIR-CHECK: condbr

// Verify LLVM IR output
// CHECK: br i1
// CHECK: br label

// OUTPUT: positive

function user_main(): number {
  const x: number = 10;

  if (x > 0) {
    console.log("positive");
  } else {
    console.log("non-positive");
  }

  return 0;
}
