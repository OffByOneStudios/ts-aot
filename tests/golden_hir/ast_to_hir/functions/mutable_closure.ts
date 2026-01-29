// Test: Mutable closure captures generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// The inner function (appears first in HIR output)
// HIR-CHECK: define @__arrow_fn_
// HIR-CHECK: load_capture
// HIR-CHECK: add.f64
// HIR-CHECK: store_capture
// HIR-CHECK: ret

// Counter function with mutable captured variable
// HIR-CHECK: define @makeCounter
// HIR-CHECK: make_closure

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function makeCounter(): () => number {
  let count = 0;
  return () => {
    count = count + 1;
    return count;
  };
}

function user_main(): number {
  const counter = makeCounter();
  console.log(counter());
  console.log(counter());
  console.log(counter());
  return 0;
}
