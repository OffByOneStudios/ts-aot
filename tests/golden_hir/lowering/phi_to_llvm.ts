// Test: PHI nodes for conditional values
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Conditional expressions should use PHI nodes
// HIR-CHECK: define @max
// HIR-CHECK: cmp.gt.f64
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64

// OUTPUT: 20
// OUTPUT: 15

function max(a: number, b: number): number {
  if (a > b) {
    return a;
  } else {
    return b;
  }
}

function user_main(): number {
  console.log(max(10, 20));  // 20
  console.log(max(15, 5));   // 15
  return 0;
}
