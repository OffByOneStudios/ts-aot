// Test: Array.slice and Array.concat methods
// XFAIL: Array.slice/concat cause compilation failure in HIR pipeline
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// slice uses runtime call
// HIR-CHECK: call "ts_array_slice"
// concat uses runtime call
// HIR-CHECK: call "ts_array_concat"
// HIR-CHECK: ret

// OUTPUT: 2,3
// OUTPUT: 1,2,3,4,5

function user_main(): number {
  const arr = [1, 2, 3, 4, 5];

  // slice: get elements from index 1 to 3
  const sliced = arr.slice(1, 3);
  console.log(sliced.join(","));

  // concat: combine two arrays
  const a = [1, 2, 3];
  const b = [4, 5];
  const combined = a.concat(b);
  console.log(combined.join(","));

  return 0;
}
