// Test: Negative zero semantics preservation
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// JavaScript has distinct +0 and -0
// 1 / 0 = Infinity, but 1 / -0 = -Infinity
// This is important for correct numeric semantics

// HIR-CHECK: define @user_main() -> f64

// Multiplication can produce -0
// 0 * -1 = -0, so 1 / (0 * -1) = -Infinity
// OUTPUT: -Infinity

// -0 + -0 = -0
// OUTPUT: -Infinity

// -0 - 0 = -0
// OUTPUT: -Infinity

// Verify positive zero behaves correctly
// 1 / 0 = Infinity
// OUTPUT: Infinity

// 0 * positive = 0 (not -0)
// OUTPUT: Infinity

function user_main(): number {
  // Create -0 through multiplication
  const negOne = -1;
  const zero = 0;
  const negZero = zero * negOne;  // -0
  console.log(1 / negZero);  // -Infinity

  // -0 + -0 = -0
  const negZero2 = zero * negOne;
  console.log(1 / (negZero + negZero2));  // -Infinity

  // -0 - 0 = -0
  console.log(1 / (negZero - zero));  // -Infinity

  // Positive zero
  const posZero = 0;
  console.log(1 / posZero);  // Infinity

  // 0 * positive = 0 (positive zero)
  const posOne = 1;
  const posZeroFromMul = zero * posOne;
  console.log(1 / posZeroFromMul);  // Infinity

  return 0;
}
