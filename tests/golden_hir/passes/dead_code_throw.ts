// Test: Dead code after throw statement
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main with try/catch (emitted first)
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.string "starting"
// HIR-CHECK: setup_try
// HIR-CHECK: ret

// Code after throw should be eliminated
// HIR-CHECK: define @throwError
// HIR-CHECK: throw
// HIR-CHECK-NOT: const.string "unreachable"

// OUTPUT: starting
// OUTPUT: caught

function throwError(): void {
  throw new Error("error");
  console.log("unreachable");  // Dead code after throw
}

function user_main(): number {
  console.log("starting");
  try {
    throwError();
  } catch (e) {
    console.log("caught");
  }
  return 0;
}
