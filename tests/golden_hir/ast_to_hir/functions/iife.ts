// Test: IIFE (Immediately Invoked Function Expression)
// XFAIL: IIFE causes compilation failure in HIR pipeline
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// IIFE creates function and calls it immediately
// HIR-CHECK: call
// HIR-CHECK: ret

// OUTPUT: 42
// OUTPUT: hello from IIFE

function user_main(): number {
  // IIFE returning value
  const result = (function(): number {
    return 42;
  })();
  console.log(result);

  // IIFE with side effects
  (function() {
    console.log("hello from IIFE");
  })();

  return 0;
}
