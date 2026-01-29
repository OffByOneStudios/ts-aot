// Test: Function declarations with parameters and return
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe
// XFAIL: HIR uses neg.i64 for float negation, should be neg.f64

// Test function with multiple parameters
// HIR-CHECK: define @add({{.*}}, {{.*}}) -> f64
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// Test function with return statement
// HIR-CHECK: define @multiply({{.*}}, {{.*}}) -> f64
// HIR-CHECK: mul.f64
// HIR-CHECK: ret

// Test function with multiple returns (early return)
// HIR-CHECK: define @abs
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: neg.f64
// HIR-CHECK: ret
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 15
// OUTPUT: 50
// OUTPUT: 5

function add(a: number, b: number, c: number): number {
  return a + b + c;
}

function multiply(x: number, y: number): number {
  const result = x * y;
  return result;
}

function abs(n: number): number {
  if (n < 0) {
    return -n;
  }
  return n;
}

function user_main(): number {
  console.log(add(3, 5, 7));
  console.log(multiply(5, 10));
  console.log(abs(-5));
  return 0;
}
