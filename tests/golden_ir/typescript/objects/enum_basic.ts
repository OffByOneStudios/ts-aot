// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_console_log_double
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 10
// OUTPUT: 20

enum Direction {
  Up,
  Down,
  Left
}

enum Status {
  Active = 10,
  Inactive = 20
}

function user_main(): void {
  // Auto-increment enums
  console.log(Direction.Up);    // 0
  console.log(Direction.Down);  // 1
  console.log(Direction.Left);  // 2

  // Explicit value enums
  console.log(Status.Active);   // 10
  console.log(Status.Inactive); // 20
}
