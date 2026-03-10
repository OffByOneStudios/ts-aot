// Test: Arrow functions generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main creates closures and calls them
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: make_closure "__arrow_fn_0"
// HIR-CHECK: call_indirect
// HIR-CHECK: make_closure "__arrow_fn_1"
// HIR-CHECK: call_indirect
// HIR-CHECK: ret

// Arrow function definitions appear after user_main
// HIR-CHECK: define @__arrow_fn_0({{.*}}) -> f64
// HIR-CHECK: mul.f64
// HIR-CHECK: ret

// HIR-CHECK: define @__arrow_fn_1({{.*}}) -> f64
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
