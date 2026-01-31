// Test: Arrow functions generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Arrow function definitions come first in HIR
// HIR-CHECK: define @__arrow_fn_0
// HIR-CHECK: mul.f64
// HIR-CHECK: ret

// HIR-CHECK: define @__arrow_fn_1
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// Then user_main
// HIR-CHECK: define @user_main() -> f64

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
