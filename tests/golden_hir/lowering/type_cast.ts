// Test: Type casting/coercion in LLVM lowering
// XFAIL: Boolean() call causes compilation failure in HIR pipeline
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Type assertions compile through
// HIR-CHECK: ret

// OUTPUT: 42
// OUTPUT: hello
// OUTPUT: true

function user_main(): number {
  // Type assertion with 'as'
  const value: any = 42;
  const num = value as number;
  console.log(num);

  // Type assertion for string
  const anyStr: any = "hello";
  const str = anyStr as string;
  console.log(str);

  // Boolean coercion
  const truthy: any = 1;
  const bool = truthy as boolean;
  console.log(Boolean(truthy));

  return 0;
}
