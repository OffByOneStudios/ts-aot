// Test: Return statements lower to LLVM ret
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR return should generate LLVM ret
// HIR-CHECK: define @getFortyTwo() -> f64
// HIR-CHECK: const.f64 42
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 42

function getFortyTwo(): number {
  return 42;
}

function user_main(): number {
  console.log(getFortyTwo());
  return 0;
}
