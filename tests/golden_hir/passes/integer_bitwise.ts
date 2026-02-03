// Test: Bitwise operations produce 32-bit integer results
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// JavaScript bitwise operations:
// - |, &, ^, <<, >> produce 32-bit signed integers
// - >>> produces 32-bit unsigned integers
// HIR-CHECK: define @user_main() -> f64

// Bitwise OR: 5 | 3 = 7
// HIR-CHECK: or.i64

// Bitwise AND: 7 & 3 = 3
// HIR-CHECK: and.i64

// Left shift: 1 << 4 = 16
// HIR-CHECK: shl.i64

// Signed right shift: -8 >> 2 = -2
// HIR-CHECK: shr.i64

// Unsigned right shift: -1 >>> 0 = 4294967295 (UINT32_MAX)
// HIR-CHECK: ushr.i64

// Return value optimized to i64
// HIR-CHECK: const.i64 0
// HIR-CHECK: ret

// OUTPUT: 7
// OUTPUT: 3
// OUTPUT: 16
// OUTPUT: -2
// OUTPUT: 4294967295

function user_main(): number {
  // Bitwise OR
  console.log(5 | 3);

  // Bitwise AND
  console.log(7 & 3);

  // Left shift
  console.log(1 << 4);

  // Signed right shift (arithmetic)
  console.log(-8 >> 2);

  // Unsigned right shift - produces uint32
  console.log(-1 >>> 0);

  return 0;
}
