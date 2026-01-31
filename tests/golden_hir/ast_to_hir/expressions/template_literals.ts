// Test: Template literal expressions in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Basic template literal
// HIR-CHECK: define @user_main() -> f64

// Template with expressions creates string concatenation
// HIR-CHECK: const.string "Hello, "
// HIR-CHECK: string.concat
// HIR-CHECK: const.string "!"
// HIR-CHECK: string.concat

// Number to string conversion
// HIR-CHECK: const.string "Sum: "
// HIR-CHECK: call "ts_string_from_double"
// HIR-CHECK: string.concat

// HIR-CHECK: ret

// OUTPUT: Hello, World!
// OUTPUT: Sum: 15
// OUTPUT: Multi-line works

function user_main(): number {
  const name: string = "World";
  const greeting = `Hello, ${name}!`;
  console.log(greeting);

  const a: number = 5;
  const b: number = 10;
  console.log(`Sum: ${a + b}`);

  // Multi-line template
  const multiLine = `Multi-line
works`;
  console.log(multiLine.split('\n')[0] + " " + multiLine.split('\n')[1]);

  return 0;
}
