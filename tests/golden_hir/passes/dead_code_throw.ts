// Test: Dead code after throw statement
// XFAIL: Throw in HIR pipeline causes compilation issues
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Code after throw should be eliminated
// HIR-CHECK: define @throwError
// HIR-CHECK: throw
// HIR-CHECK-NOT: const.string "unreachable"
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64

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
