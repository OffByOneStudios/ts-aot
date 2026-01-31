// Test: Dead code elimination for if(true)
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// When condition is always true, else branch should be eliminated
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.string "always"
// HIR-CHECK-NOT: const.string "never"
// HIR-CHECK: ret

// OUTPUT: always

function user_main(): number {
  if (true) {
    console.log("always");
  } else {
    console.log("never");  // Dead code
  }
  return 0;
}
