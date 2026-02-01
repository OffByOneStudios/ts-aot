// Test: Enum types compile to constants
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: Red

enum Color {
  Red,
  Green,
  Blue
}

enum Direction {
  Up = "up",
  Down = "down"
}

function user_main(): number {
  // Numeric enum values
  console.log(Color.Red);
  console.log(Color.Green);
  console.log(Color.Blue);

  // Reverse mapping
  console.log(Color[0]);

  return 0;
}
