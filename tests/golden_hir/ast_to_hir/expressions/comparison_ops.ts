// Test: Comparison operators generate correct HIR opcodes
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 10
// HIR-CHECK: const.f64 5
// HIR-CHECK: cmp.eq.f64
// HIR-CHECK: cmp.ne.f64
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: cmp.le.f64
// HIR-CHECK: cmp.gt.f64
// HIR-CHECK: cmp.ge.f64
// HIR-CHECK: ret

// OUTPUT: false
// OUTPUT: true
// OUTPUT: false
// OUTPUT: false
// OUTPUT: true
// OUTPUT: true

function user_main(): number {
  const a: number = 10;
  const b: number = 5;

  console.log(a == b);   // false - cmp.eq.f64
  console.log(a != b);   // true - cmp.ne.f64
  console.log(a < b);    // false - cmp.lt.f64
  console.log(a <= b);   // false - cmp.le.f64
  console.log(a > b);    // true - cmp.gt.f64
  console.log(a >= b);   // true - cmp.ge.f64

  return 0;
}
