// Test: Closures generate make_closure and load_capture
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// Arrow function definition comes first
// HIR-CHECK: define @__arrow_fn_0
// HIR-CHECK: load_capture "count"
// HIR-CHECK: add.f64
// HIR-CHECK: store_capture "count"
// HIR-CHECK: ret

// Then makeCounter definition
// HIR-CHECK: define @makeCounter
// HIR-CHECK: alloca
// HIR-CHECK: store
// HIR-CHECK: make_closure

// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function makeCounter(): () => number {
  let count: number = 0;
  return (): number => {
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
