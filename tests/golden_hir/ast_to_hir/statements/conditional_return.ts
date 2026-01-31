// Test: Conditional return statements
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @sign(f64 %x) -> f64
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: neg.f64
// HIR-CHECK: ret
// HIR-CHECK: cmp.gt.f64
// HIR-CHECK: condbr
// HIR-CHECK: const.f64 1
// HIR-CHECK: ret
// HIR-CHECK: const.f64 0
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64

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
