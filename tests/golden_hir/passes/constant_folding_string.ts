// Test: Constant folding for string concatenation
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// String concatenation should be folded at compile time
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.string "Hello, World!"
// HIR-CHECK-NOT: string.concat
// HIR-CHECK: ret

// OUTPUT: Hello, World!

function user_main(): number {
  console.log("Hello, " + "World!");
  return 0;
}
