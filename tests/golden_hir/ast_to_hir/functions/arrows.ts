// Test: Arrow functions generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe
// XFAIL: HIR-to-LLVM crash when calling stored function pointer

// Simple arrow function with expression body
// HIR-CHECK: define @user_main() -> f64

// Arrow with expression body - should have implicit return
// HIR-CHECK: define @{{.*}}arrow
// HIR-CHECK: mul.f64
// HIR-CHECK: ret

// Arrow with block body - should have explicit return
// HIR-CHECK: define @{{.*}}arrow
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// OUTPUT: 10
// OUTPUT: 7

function user_main(): number {
  // Arrow function with expression body (implicit return)
  const double = (x: number) => x * 2;
  console.log(double(5));

  // Arrow function with block body (explicit return)
  const addThree = (x: number) => {
    const result = x + 3;
    return result;
  };
  console.log(addThree(4));

  return 0;
}
