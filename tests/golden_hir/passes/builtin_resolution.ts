// Test: Builtin resolution replaces console.log with direct runtime call
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// console.log should resolve to ts_console_log call
// The HIR output shows both before and after optimization
// HIR-CHECK-DAG: call "ts_console_log"
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: Hello, World!

function user_main(): number {
  console.log("Hello, World!");
  return 0;
}
