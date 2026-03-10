// Test: Conditional return statements
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main calls sign_dbl (monomorphized name)
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "sign_dbl"
// HIR-CHECK: ret

// sign function with multiple conditional returns (monomorphized as sign_dbl)
// HIR-CHECK: define @sign_dbl(f64 %x) -> f64
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// Return values: -1 stays f64, 1 and 0 optimized to i64
// HIR-CHECK: const.f64 -1.000000
// HIR-CHECK: ret
// HIR-CHECK: cmp.gt.f64
// HIR-CHECK: condbr
// HIR-CHECK: const.i64 1
// HIR-CHECK: ret
// HIR-CHECK: const.i64 0
// HIR-CHECK: ret

// OUTPUT: -1
// OUTPUT: 0
// OUTPUT: 1

function sign(x: number): number {
  if (x < 0) return -1;
  if (x > 0) return 1;
  return 0;
}

function user_main(): number {
  console.log(sign(-5));
  console.log(sign(0));
  console.log(sign(5));
  return 0;
}
