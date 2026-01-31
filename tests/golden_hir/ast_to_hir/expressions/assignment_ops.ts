// Test: Assignment operators generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: alloca
// HIR-CHECK: load
// HIR-CHECK: store
// HIR-CHECK: ret

// OUTPUT: 15
// OUTPUT: 10
// OUTPUT: 20
// OUTPUT: 4

function user_main(): number {
  let x: number = 10;

  x += 5;
  console.log(x);  // 15

  x -= 5;
  console.log(x);  // 10

  x *= 2;
  console.log(x);  // 20

  x /= 5;
  console.log(x);  // 4

  return 0;
}
