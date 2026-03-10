// Test: Void functions generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// printMessage is inlined into user_main
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.string "Hello from function!"
// HIR-CHECK: call "ts_console_log"
// HIR-CHECK: ret

// Separate printMessage definition still exists
// HIR-CHECK: define @printMessage() -> void
// HIR-CHECK: const.string "Hello from function!"
// HIR-CHECK: ret void

// OUTPUT: Hello from function!
// OUTPUT: done

function printMessage(): void {
  console.log("Hello from function!");
}

function user_main(): number {
  printMessage();
  console.log("done");
  return 0;
}
