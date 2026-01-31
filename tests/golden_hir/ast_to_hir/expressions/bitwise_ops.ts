// Test: Bitwise operators in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Bitwise operations generate i64 instructions
// HIR-CHECK: define @user_main() -> f64

// Bitwise AND
// HIR-CHECK: and.i64

// Bitwise OR
// HIR-CHECK: or.i64

// Bitwise XOR
// HIR-CHECK: xor.i64

// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 7
// OUTPUT: 6

function user_main(): number {
  const a: number = 5;  // binary: 101
  const b: number = 3;  // binary: 011

  // Bitwise AND: 101 & 011 = 001
  console.log(a & b);  // 1

  // Bitwise OR: 101 | 011 = 111
  console.log(a | b);  // 7

  // Bitwise XOR: 101 ^ 011 = 110
  console.log(a ^ b);  // 6

  return 0;
}
