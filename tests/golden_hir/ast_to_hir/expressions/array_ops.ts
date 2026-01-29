// Test: Array operations generate correct HIR opcodes
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_array.boxed
// HIR-CHECK: call_method {{.*}}, "push"
// HIR-CHECK: call_method {{.*}}, "push"
// HIR-CHECK: call_method {{.*}}, "push"
// HIR-CHECK: ret

// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3
// OUTPUT: 3

function user_main(): number {
  const arr: number[] = [];
  arr.push(1);
  arr.push(2);
  arr.push(3);

  console.log(arr[0]);
  console.log(arr[1]);
  console.log(arr[2]);
  console.log(arr.length);

  return 0;
}
