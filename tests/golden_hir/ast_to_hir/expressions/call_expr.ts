// Test: Function call expressions in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @add({{.*}}) -> f64
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 10
// HIR-CHECK: const.f64 20
// HIR-CHECK: call "add"
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
