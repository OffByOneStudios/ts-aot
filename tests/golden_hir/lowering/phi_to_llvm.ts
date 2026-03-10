// Test: PHI nodes for conditional values
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main calls max (emitted first)
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "max_dbl_dbl"
// HIR-CHECK: ret

// Conditional expressions in max (monomorphized as max_dbl_dbl)
// HIR-CHECK: define @max_dbl_dbl
// HIR-CHECK: cmp.gt.f64
// HIR-CHECK: condbr
// HIR-CHECK: ret

// OUTPUT: 20
// OUTPUT: 15

function max(a: number, b: number): number {
  if (a > b) {
    return a;
  } else {
    return b;
  }
}

function user_main(): number {
  console.log(max(10, 20));  // 20
  console.log(max(15, 5));   // 15
  return 0;
}
