// Test: Console method resolution
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// console.log should be resolved to builtin
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: hello
// OUTPUT: 42
// OUTPUT: 3.14

function user_main(): number {
  console.log("hello");
  console.log(42);
  console.log(3.14);
  return 0;
}
