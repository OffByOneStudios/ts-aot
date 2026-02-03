// Test: Bitwise operations produce 32-bit integer results
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// JavaScript bitwise operations:
// - |, &, ^, <<, >> produce 32-bit signed integers
// - >>> produces 32-bit unsigned integers
// HIR-CHECK: define @user_main() -> f64

// Variables prevent constant folding, so we can see HIR bitwise ops
// HIR-CHECK: or.i64
// HIR-CHECK: and.i64
// HIR-CHECK: shl.i64
// HIR-CHECK: shr.i64
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
  // Use variables to prevent constant folding
  const a = 5;
  const b = 3;
  const c = 7;
  const d = 1;
  const e = 4;
  const f = -8;
  const g = 2;
  const h = -1;
  const zero = 0;

  // Bitwise OR: 5 | 3 = 7
  console.log(a | b);

  // Bitwise AND: 7 & 3 = 3
  console.log(c & b);

  // Left shift: 1 << 4 = 16
  console.log(d << e);

  // Signed right shift (arithmetic): -8 >> 2 = -2
  console.log(f >> g);

  // Unsigned right shift - produces uint32: -1 >>> 0 = 4294967295
  console.log(h >>> zero);

  return 0;
}
