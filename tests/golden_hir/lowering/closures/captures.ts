// Test: Closure capture lowering to LLVM
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Arrow function is defined first (inner function hoisted)
// HIR-CHECK: define @__arrow_fn_0
// HIR-CHECK: load_capture "count"
// HIR-CHECK: add.f64
// HIR-CHECK: store_capture
// HIR-CHECK: ret

// Then makeAccumulator uses make_closure
// HIR-CHECK: define @makeAccumulator
// HIR-CHECK: make_closure

// Finally user_main
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 10
// OUTPUT: 20
// OUTPUT: 30

function makeAccumulator(step: number): () => number {
  let count: number = 0;
  return (): number => {
    count = count + step;
    return count;
  };
}

function user_main(): number {
  const acc = makeAccumulator(10);
  console.log(acc());  // 10
  console.log(acc());  // 20
  console.log(acc());  // 30
  return 0;
}
