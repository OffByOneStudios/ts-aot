// Test: Functions with multiple return paths
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main calls classify_dbl (monomorphized name)
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "classify_dbl"
// HIR-CHECK: ret

// classify_dbl has multiple return paths with string literals
// HIR-CHECK: define @classify_dbl(f64 %{{.*}}) -> string
// HIR-CHECK: cmp.lt.f64
// HIR-CHECK: condbr
// HIR-CHECK: const.string "negative"
// HIR-CHECK: ret
// HIR-CHECK: const.string "zero"
// HIR-CHECK: ret
// HIR-CHECK: const.string "positive"
// HIR-CHECK: ret

// OUTPUT: negative
// OUTPUT: zero
// OUTPUT: positive

function classify(n: number): string {
  if (n < 0) {
    return "negative";
  } else if (n === 0) {
    return "zero";
  } else {
    return "positive";
  }
}

function user_main(): number {
  console.log(classify(-5));
  console.log(classify(0));
  console.log(classify(5));
  return 0;
}
