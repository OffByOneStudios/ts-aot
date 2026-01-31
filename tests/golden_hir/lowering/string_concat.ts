// Test: String concatenation lowering to LLVM
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// String concat operations lower to runtime calls
// HIR-CHECK: define @user_main() -> f64

// String constants and concatenation
// HIR-CHECK: const.string "Hello"
// HIR-CHECK: const.string " "
// HIR-CHECK: const.string "World"
// HIR-CHECK: string.concat
// HIR-CHECK: string.concat

// HIR-CHECK: ret

// OUTPUT: Hello World

function user_main(): number {
  const greeting: string = "Hello";
  const space: string = " ";
  const name: string = "World";
  const result = greeting + space + name;
  console.log(result);
  return 0;
}
