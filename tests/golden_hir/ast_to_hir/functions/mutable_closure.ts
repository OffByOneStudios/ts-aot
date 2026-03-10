// Test: Mutable closure captures generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// makeCounter is inlined into user_main
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: make_closure "__arrow_fn_0"
// HIR-CHECK: call_indirect
// HIR-CHECK: ret

// Separate makeCounter definition
// HIR-CHECK: define @makeCounter() -> () -> f64
// HIR-CHECK: make_closure "__arrow_fn_0"

// Arrow function with mutable capture
// HIR-CHECK: define @__arrow_fn_0({{.*}}) -> f64
// HIR-CHECK: load_capture "count"
// HIR-CHECK: add.f64
// HIR-CHECK: store_capture "count"
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
