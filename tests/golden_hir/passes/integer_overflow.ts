// Test: Integer overflow detection prevents unsafe conversions
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// The constant folding pass folds literals at compile time
// The IntegerOptimizationPass then decides whether to use i64 or f64
// HIR-CHECK: define @user_main() -> f64

// Safe folded constants: 5000 is within safe integer range
// Goes to console.log which is a function call - stays f64
// HIR-CHECK: const.f64 5000

// Large folded constant: 18014398509480000 exceeds MAX_SAFE_INTEGER
// But since it's going to console.log, it stays f64 anyway
// HIR-CHECK: const.f64 18014398509480000

// Division result: 5.0 (always f64 since it came from division)
// HIR-CHECK: const.f64 5

// Return value 0 - optimized to i64
// HIR-CHECK: const.i64 0
// HIR-CHECK: ret

// OUTPUT: 5000
// OUTPUT: 1.80144e+16
// OUTPUT: 5

function user_main(): number {
  // Safe multiplication - constant folded at compile time
  console.log(100 * 50);

  // Overflow case: result exceeds MAX_SAFE_INTEGER
  console.log(9007199254740000 * 2);

  // Division result
  console.log(10 / 2);

  return 0;
}
