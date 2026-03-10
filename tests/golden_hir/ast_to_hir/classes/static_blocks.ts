// Test: Static blocks execute during class initialization
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: global @Counter_static_count
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: load {{.*}}, @Counter_static_count
// HIR-CHECK: call "ts_console_log"
// HIR-CHECK: ret

// OUTPUT: 0

class Counter {
  static count: number = 0;

  static {
    Counter.count = 42;
    console.log("Static block executed");
  }
}

function user_main(): number {
  console.log(Counter.count);
  return 0;
}
