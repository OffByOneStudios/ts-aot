// Test: Basic function definition and call
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// add is inlined into user_main, but separate definition still exists
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// Monomorphized function definition
// HIR-CHECK: define @add_dbl_dbl(f64 %{{.*}}, f64 %{{.*}}) -> f64
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// OUTPUT: 8

function add(a: number, b: number): number {
  return a + b;
}

function user_main(): number {
  const result = add(3, 5);
  console.log(result);
  return 0;
}
