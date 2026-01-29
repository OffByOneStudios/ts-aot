// Test: Array method resolution pass transforms call_method to specialized calls
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe
// XFAIL: HIR-to-LLVM lowering - CallMethod for map/forEach not fully implemented

// After MethodResolutionPass, array.map should become a call to ts_array_map
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "ts_array_map"
// HIR-CHECK: ret

// OUTPUT: 2
// OUTPUT: 4
// OUTPUT: 6

function user_main(): number {
  const arr: number[] = [1, 2, 3];
  const doubled = arr.map((x: number): number => x * 2);
  doubled.forEach((v: number): void => console.log(v));
  return 0;
}
