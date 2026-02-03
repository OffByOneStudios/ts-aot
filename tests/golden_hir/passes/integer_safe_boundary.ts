// Test: Safe integer boundary behavior with runtime operations
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// When using local variables, operations happen at runtime
// Constants go to f64 allocas, so they stay f64
// HIR-CHECK: define @user_main() -> f64

// Variables stored to f64 allocas - constants stay f64
// HIR-CHECK: const.f64 1000
// HIR-CHECK: const.f64 2000
// HIR-CHECK: add.f64

// Multiplication at runtime
// HIR-CHECK: const.f64 100
// HIR-CHECK: const.f64 100
// HIR-CHECK: mul.f64

// Division at runtime
// HIR-CHECK: const.f64 10
// HIR-CHECK: const.f64 2
// HIR-CHECK: div.f64

// Return value optimized to i64
// HIR-CHECK: const.i64 0
// HIR-CHECK: ret

// OUTPUT: 3000
// OUTPUT: 10000
// OUTPUT: 5

function user_main(): number {
  // Addition with local variables - runtime operation
  const a = 1000;
  const b = 2000;
  console.log(a + b);

  // Multiplication with local variables - runtime operation
  const c = 100;
  const d = 100;
  console.log(c * d);

  // Division with local variables - always f64
  const e = 10;
  const f = 2;
  console.log(e / f);

  return 0;
}
