// Test: Edge cases for JavaScript bitwise operation semantics
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// JavaScript bitwise operations convert to 32-bit integers:
// - |, &, ^, <<, >> produce signed 32-bit integers (-2147483648 to 2147483647)
// - >>> produces unsigned 32-bit integers (0 to 4294967295)
// - ~ produces signed 32-bit integers

// HIR-CHECK: define @user_main() -> f64

// XOR: 0xFF ^ 0x0F = 0xF0 (240)
// OUTPUT: 240

// Bitwise NOT: ~0 = -1 (all bits set in 32-bit signed)
// OUTPUT: -1

// NOT of 1: ~1 = -2
// OUTPUT: -2

// Large number truncation: (2^32 + 5) | 0 = 5 (upper bits truncated)
// OUTPUT: 5

// Negative unsigned shift: -1 >>> 16 = 65535 (0xFFFF)
// OUTPUT: 65535

// Runtime shift amount masking: 1 << (32 stored in variable)
// Should be 1 << (32 & 0x1F) = 1 << 0 = 1
// OUTPUT: 1

// Runtime shift amount masking: 1 << (33 stored in variable)
// Should be 1 << (33 & 0x1F) = 1 << 1 = 2
// OUTPUT: 2

// Max signed 32-bit: 0x7FFFFFFF | 0 = 2147483647
// OUTPUT: 2147483647

// Runtime 0x80000000 | 0 should give -2147483648 (signed interpretation)
// OUTPUT: -2147483648

function user_main(): number {
  // XOR
  console.log(0xFF ^ 0x0F);

  // Bitwise NOT
  console.log(~0);
  console.log(~1);

  // Large number truncation (values > 2^32 get truncated to 32 bits)
  console.log((4294967296 + 5) | 0);

  // Unsigned right shift of negative number
  console.log(-1 >>> 16);

  // Shift amount masking - use variables to force runtime evaluation
  // (constant folding doesn't apply JS 32-bit semantics yet)
  const shift32 = 32;
  const shift33 = 33;
  console.log(1 << shift32);  // 1 << (32 & 0x1F) = 1 << 0 = 1
  console.log(1 << shift33);  // 1 << (33 & 0x1F) = 1 << 1 = 2

  // Boundary values
  console.log(0x7FFFFFFF | 0);  // Max signed int32

  // Use variable to force runtime evaluation of signed 32-bit boundary
  const largeVal = 0x80000000;
  console.log(largeVal | 0);   // Should be -2147483648 when treated as signed

  return 0;
}
