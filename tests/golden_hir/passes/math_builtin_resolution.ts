// Test: Math builtin resolution pass transforms Math calls
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe
// XFAIL: HIR-to-LLVM lowering - ts_math_floor/ceil parameter type mismatch

// After BuiltinResolutionPass, Math.floor should become a call to ts_math_floor
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "ts_math_floor"
// HIR-CHECK: call "ts_math_ceil"
// HIR-CHECK: ret

// OUTPUT: 3
// OUTPUT: 4

function user_main(): number {
  const a: number = Math.floor(3.7);
  const b: number = Math.ceil(3.2);
  console.log(a);
  console.log(b);
  return 0;
}
