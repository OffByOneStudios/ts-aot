// Test: Closures generate make_closure and load_capture
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: HIR pipeline does not yet support closure lowering

// HIR-CHECK: define @makeCounter
// HIR-CHECK: alloca
// HIR-CHECK: store
// HIR-CHECK: make_closure

// HIR-CHECK: define @__arrow_fn_
// HIR-CHECK: load_capture
// HIR-CHECK: add.f64
// HIR-CHECK: store_capture
// HIR-CHECK: ret

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
