// Test: Closure capture lowering to LLVM
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main is emitted first (with inlined makeAccumulator)
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: make_closure "__arrow_fn_0"
// HIR-CHECK: call_indirect
// HIR-CHECK: ret

// makeAccumulator emitted after (monomorphized as makeAccumulator_dbl)
// HIR-CHECK: define @makeAccumulator_dbl
// HIR-CHECK: make_closure

// Arrow function captures count and step
// HIR-CHECK: define @__arrow_fn_0
// HIR-CHECK: load_capture "count"
// HIR-CHECK: add.f64
// HIR-CHECK: store_capture
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
