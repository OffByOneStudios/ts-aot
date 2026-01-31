// Test: Object method resolution
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Object.keys should be resolved to builtin
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: a,b

function user_main(): number {
  const obj = { a: 1, b: 2 };
  const keys = Object.keys(obj);
  console.log(keys.join(","));
  return 0;
}
