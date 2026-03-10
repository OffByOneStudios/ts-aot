// Test: Return statements generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main with inlined getValue call
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.i64 42
// HIR-CHECK: call "ts_console_log"
// HIR-CHECK: ret

// getValue function definition (emitted after user_main)
// HIR-CHECK: define @getValue() -> f64
// Return values optimized to i64 when not used in f64 context
// HIR-CHECK: const.i64 42
// HIR-CHECK: ret

// OUTPUT: 42

function getValue(): number {
  return 42;
}

function user_main(): number {
  console.log(getValue());
  return 0;
}
