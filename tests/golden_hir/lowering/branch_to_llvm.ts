// Test: Branches lower to LLVM br instructions
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR branches should generate LLVM br and conditional br
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: branch
// HIR-CHECK: ret

// OUTPUT: positive

function user_main(): number {
  const x: number = 5;

  if (x > 0) {
    console.log("positive");
  } else {
    console.log("non-positive");
  }

  return 0;
}
