// Test: Arithmetic operations generate correct HIR opcodes
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 10
// HIR-CHECK: const.f64 5
// HIR-CHECK: add.f64
// HIR-CHECK: sub.f64
// HIR-CHECK: mul.f64
// HIR-CHECK: div.f64
// HIR-CHECK: mod.f64
// HIR-CHECK: ret

// OUTPUT: 15
// OUTPUT: 5
// OUTPUT: 50
// OUTPUT: 2
// OUTPUT: 0

function user_main(): number {
  const a: number = 10;
  const b: number = 5;

  console.log(a + b);  // 15 - add.f64
  console.log(a - b);  // 5 - sub.f64
  console.log(a * b);  // 50 - mul.f64
  console.log(a / b);  // 2 - div.f64
  console.log(a % b);  // 0 - mod.f64

  return 0;
}
