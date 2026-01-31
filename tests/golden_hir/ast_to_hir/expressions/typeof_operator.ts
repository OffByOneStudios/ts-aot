// Test: typeof operator in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// typeof generates HIR typeof instruction
// HIR-CHECK: define @user_main() -> f64

// typeof for string and object (pointer types)
// HIR-CHECK: typeof
// HIR-CHECK: typeof

// HIR-CHECK: ret

// OUTPUT: string
// OUTPUT: object

function user_main(): number {
  const str: string = "hello";
  const obj = { x: 1 };

  console.log(typeof str);
  console.log(typeof obj);

  return 0;
}
