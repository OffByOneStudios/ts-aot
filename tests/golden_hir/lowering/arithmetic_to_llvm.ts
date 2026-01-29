// Test: HIR arithmetic operations
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// Verify HIR arithmetic opcodes exist
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 5
// HIR-CHECK: const.f64 7
// HIR-CHECK: mul.f64
// HIR-CHECK: ret

// OUTPUT: 35

function user_main(): number {
  const a: number = 5;
  const b: number = 7;
  console.log(a * b);  // 35
  return 0;
}
