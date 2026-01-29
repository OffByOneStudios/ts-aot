// Test: Constant folding optimizes compile-time arithmetic
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// After constant folding, 2 + 3 should become const.f64 5.0
// The HIR output shows both before and after optimization
// We verify the folded constant exists in the output
// HIR-CHECK-DAG: const.f64 5.000000
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 5

function user_main(): number {
  const x = 2 + 3;  // Should be folded to 5 at compile time
  console.log(x);
  return 0;
}
