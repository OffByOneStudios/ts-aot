// Test: Return statements generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @getValue() -> f64
// HIR-CHECK: const.f64 42
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "getValue"
// HIR-CHECK: ret

// OUTPUT: 42

function getValue(): number {
  return 42;
}

function user_main(): number {
  console.log(getValue());
  return 0;
}
