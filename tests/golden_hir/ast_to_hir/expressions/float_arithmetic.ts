// Test: Float arithmetic generates correct HIR f64 opcodes
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 3.14
// HIR-CHECK: const.f64 2.71
// HIR-CHECK: add.f64
// HIR-CHECK: mul.f64
// HIR-CHECK: div.f64
// HIR-CHECK: ret

// OUTPUT: 5.85
// OUTPUT: 8.5094
// OUTPUT: 1.15867

function user_main(): number {
  const pi: number = 3.14;
  const e: number = 2.71;

  console.log(pi + e);    // 5.85
  console.log(pi * e);    // 8.5094
  console.log(pi / e);    // ~1.1587

  return 0;
}
