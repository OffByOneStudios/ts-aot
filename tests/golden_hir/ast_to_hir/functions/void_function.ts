// Test: Void functions generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @printMessage
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "printMessage"
// HIR-CHECK: ret

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
