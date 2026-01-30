// Test: Static blocks execute during class initialization
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: global @Counter_static_count
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: store {{.*}}, @Counter_static_count
// HIR-CHECK: const.string "Static block executed"
// HIR-CHECK: call_method {{.*}}, "log"
// HIR-CHECK: ret

// OUTPUT: Static block executed
// OUTPUT: 42

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
