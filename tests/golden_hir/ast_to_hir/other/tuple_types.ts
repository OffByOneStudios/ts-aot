// Test: Tuple types are handled as arrays
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Tuples create array literals
// HIR-CHECK: new_array
// HIR-CHECK: ret

// OUTPUT: Alice
// OUTPUT: 30
// OUTPUT: 1

function user_main(): number {
  // Basic tuple
  const person: [string, number] = ["Alice", 30];
  console.log(person[0]);
  console.log(person[1]);

  // Tuple with different types
  const mixed: [number, string, boolean] = [1, "hello", true];
  console.log(mixed[0]);

  return 0;
}
