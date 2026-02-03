// Test: Integer optimization pass converts Float64 integer literals to Int64
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Integer literals stored to typed variables stay f64 (safe for type consistency)
// HIR-CHECK: define @user_main() -> f64

// Variables declared as number create f64 allocas, so constants stay f64
// HIR-CHECK: const.f64 5
// HIR-CHECK: const.f64 3
// HIR-CHECK: add.f64

// Division also stays f64 (JavaScript semantics: 4/2 = 2.0)
// HIR-CHECK: const.f64 10
// HIR-CHECK: const.f64 2
// HIR-CHECK: div.f64

// But return value constant IS converted to i64 (not used in f64 context)
// HIR-CHECK: const.i64 0
// HIR-CHECK: ret

// OUTPUT: 8
// OUTPUT: 5
// OUTPUT: 20

function user_main(): number {
  // Constants stored to number variables stay f64
  const a: number = 5;
  const b: number = 3;
  const sum = a + b;
  console.log(sum);

  // Division - always f64 (JavaScript semantics)
  const c: number = 10;
  const d: number = 2;
  const quotient = c / d;
  console.log(quotient);

  // Multiplication also stays f64 when stored to variables
  const e: number = 4;
  const f: number = 5;
  const product = e * f;
  console.log(product);

  // Return 0 - this constant CAN be optimized to i64
  return 0;
}
