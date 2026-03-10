// Test: Function call expressions in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// The add function is inlined into user_main, but also emitted separately
// HIR-CHECK: define @user_main() -> f64
// Inlined function call: arguments are const.f64, result is add.f64
// HIR-CHECK: const.f64 10
// HIR-CHECK: const.f64 20
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// Original add function is also emitted
// HIR-CHECK: define @add_dbl_dbl({{.*}}) -> f64
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// OUTPUT: 30

function add(a: number, b: number): number {
  return a + b;
}

function user_main(): number {
  const result = add(10, 20);
  console.log(result);
  return 0;
}
