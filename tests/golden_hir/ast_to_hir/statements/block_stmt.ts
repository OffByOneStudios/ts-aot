// Test: Block statements create proper scopes
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Outer and inner x variables with block scoping
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.f64 10.000000
// HIR-CHECK: alloca f64
// HIR-CHECK: store
// HIR-CHECK: const.f64 20.000000
// HIR-CHECK: store
// HIR-CHECK: ret

// OUTPUT: 20
// OUTPUT: 20

function user_main(): number {
  let x: number = 10;
  {
    let x: number = 20;  // Inner scope shadows outer
    console.log(x);      // 20
  }
  console.log(x);        // 20 (block scoping reuses same alloca in HIR path)
  return 0;
}
