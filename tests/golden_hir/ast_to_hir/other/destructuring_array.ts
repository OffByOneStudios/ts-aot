// Test: Array destructuring
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_array
// HIR-CHECK: get_elem
// HIR-CHECK: store
// HIR-CHECK: get_elem
// HIR-CHECK: store
// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 2

function user_main(): number {
  const [a, b] = [1, 2, 3];

  console.log(a);  // 1
  console.log(b);  // 2

  return 0;
}
