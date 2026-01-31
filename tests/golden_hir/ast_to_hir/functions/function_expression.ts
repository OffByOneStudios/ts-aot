// Test: Function expressions generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: alloca
// HIR-CHECK: store
// HIR-CHECK: load
// HIR-CHECK: call_indirect
// HIR-CHECK: ret

// OUTPUT: 25

function user_main(): number {
  const square = function(x: number): number {
    return x * x;
  };

  console.log(square(5));
  return 0;
}
