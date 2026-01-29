// Test: Variable declarations generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 10
// HIR-CHECK: %x = alloca f64
// HIR-CHECK: store {{.*}}, %x
// HIR-CHECK: const.f64 20
// HIR-CHECK: %y = alloca f64
// HIR-CHECK: store {{.*}}, %y
// HIR-CHECK: load f64, %x
// HIR-CHECK: load f64, %y
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// OUTPUT: 30

function user_main(): number {
  const x: number = 10;
  let y: number = 20;
  console.log(x + y);
  return 0;
}
