// Test: BigInt literals generate correct HIR
// XFAIL: BigInt arithmetic uses i64 ops instead of ts_bigint_* calls in HIR pipeline
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "ts_bigint_create_str"
// HIR-CHECK: add.i64
// HIR-CHECK: mul.i64
// HIR-CHECK: ret

// OUTPUT: 30
// OUTPUT: 200

function user_main(): number {
  // Basic BigInt literals and arithmetic
  const a = 10n;
  const b = 20n;

  // Arithmetic operations
  const sum = a + b;
  const prod = a * b;

  console.log(sum);   // 30
  console.log(prod);  // 200

  return 0;
}
