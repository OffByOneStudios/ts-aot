// Test: Boolean operations lower to LLVM and/or
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR boolean ops should lower correctly
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.bool true
// HIR-CHECK: const.bool false
// HIR-CHECK: ret

// OUTPUT: true
// OUTPUT: false
// OUTPUT: true
// OUTPUT: false

function user_main(): number {
  const t: boolean = true;
  const f: boolean = false;

  console.log(t && t);  // true
  console.log(t && f);  // false
  console.log(t || f);  // true
  console.log(f || f);  // false

  return 0;
}
