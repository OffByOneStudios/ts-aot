// Test: Spread operator generates correct HIR
// XFAIL: Spread operator doesn't copy source array elements (only appends new elements)
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// First array uses new_array.boxed, spread creates via ts_array_create
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_array.boxed
// HIR-CHECK: call "ts_array_create"
// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3
// OUTPUT: 4
// OUTPUT: 5

function user_main(): number {
  const arr1: number[] = [1, 2, 3];
  const arr2: number[] = [...arr1, 4, 5];

  for (const val of arr2) {
    console.log(val);
  }

  return 0;
}
