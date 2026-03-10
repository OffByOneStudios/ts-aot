// Test: Dead code elimination removes unreachable code
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main with inlined earlyReturn (emitted first)
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.string "hello"
// HIR-CHECK: ret

// After early return, dead code should be eliminated
// HIR-CHECK: define @earlyReturn
// The second console.log should not appear after optimization
// HIR-CHECK-NOT: const.string "never printed"
// HIR-CHECK: ret

// OUTPUT: hello
// OUTPUT: 10

function earlyReturn(): number {
  console.log("hello");
  return 10;
  // Dead code after return - should be eliminated
  console.log("never printed");
  return 20;
}

function user_main(): number {
  const result = earlyReturn();
  console.log(result);
  return 0;
}
