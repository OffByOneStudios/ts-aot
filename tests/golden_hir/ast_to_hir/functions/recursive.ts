// Test: Recursive functions generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @factorial
// HIR-CHECK: const.f64 1
// HIR-CHECK: ret
// HIR-CHECK: call "factorial"
// HIR-CHECK: mul.f64
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64

// OUTPUT: 1
// OUTPUT: 120
// OUTPUT: 3628800

function factorial(n: number): number {
  if (n <= 1) {
    return 1;
  }
  return n * factorial(n - 1);
}

function user_main(): number {
  console.log(factorial(0));
  console.log(factorial(5));
  console.log(factorial(10));
  return 0;
}
