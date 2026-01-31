// Test: Runtime calls lower to LLVM external calls
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Runtime calls should generate external function calls
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: hello world

function user_main(): number {
  console.log("hello world");
  return 0;
}
