// Test: String operations in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.string "hello"
// HIR-CHECK: const.string "world"
// HIR-CHECK: ret

// OUTPUT: hello world

function user_main(): number {
  const a: string = "hello";
  const b: string = "world";
  console.log(a + " " + b);
  return 0;
}
