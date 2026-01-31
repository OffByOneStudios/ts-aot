// Test: Functions with multiple return paths
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @classify
// HIR-CHECK: const.string "negative"
// HIR-CHECK: ret
// HIR-CHECK: const.string "zero"
// HIR-CHECK: ret
// HIR-CHECK: const.string "positive"
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64

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
