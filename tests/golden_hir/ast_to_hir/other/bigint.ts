// Test: BigInt literals generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "ts_bigint_create_str"
// HIR-CHECK: call "ts_bigint_add"
// HIR-CHECK: call "ts_bigint_mul"
// HIR-CHECK: ret

// OUTPUT: 30n
// OUTPUT: 200n

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
