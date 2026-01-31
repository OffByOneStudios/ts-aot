// Test: Object destructuring
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object
// HIR-CHECK: get_prop
// HIR-CHECK: store
// HIR-CHECK: get_prop
// HIR-CHECK: store
// HIR-CHECK: ret

// OUTPUT: 10
// OUTPUT: 20

function user_main(): number {
  const obj = { x: 10, y: 20 };
  const { x, y } = obj;

  console.log(x);  // 10
  console.log(y);  // 20

  return 0;
}
