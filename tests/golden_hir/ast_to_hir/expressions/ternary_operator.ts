// Test: Ternary/conditional operator in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Ternary operator generates condbr + phi (branch-based selection)
// HIR-CHECK: define @user_main() -> f64

// First ternary: condition check and branch
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: const.string "small"
// HIR-CHECK: const.string "large"
// HIR-CHECK: phi

// Second ternary with numbers
// HIR-CHECK: cmp.gt.f64
// HIR-CHECK: condbr
// HIR-CHECK: phi

// HIR-CHECK: ret

// OUTPUT: small
// OUTPUT: 20
// OUTPUT: one

function user_main(): number {
  const x: number = 5;

  // Basic ternary
  const result = x < 10 ? "small" : "large";
  console.log(result);

  // Ternary with expressions
  const a: number = 10;
  const b: number = 20;
  const max = a > b ? a : b;
  console.log(max);

  // Nested ternary
  const val: number = 1;
  const str = val === 0 ? "zero" : val === 1 ? "one" : "other";
  console.log(str);

  return 0;
}
