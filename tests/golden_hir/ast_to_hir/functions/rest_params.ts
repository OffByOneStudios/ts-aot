// Test: Rest parameters create array from call-site arguments
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @sum
// Rest parameter is an array - accessed via get_elem
// HIR-CHECK: get_elem
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// Call site packages arguments into array
// HIR-CHECK: new_array.boxed
// HIR-CHECK: set_elem
// HIR-CHECK: call "sum"
// HIR-CHECK: ret

// OUTPUT: 10
// OUTPUT: 6

function sum(...nums: number[]): number {
  let total: number = 0;
  for (let i: number = 0; i < nums.length; i++) {
    total += nums[i];
  }
  return total;
}

function user_main(): number {
  console.log(sum(1, 2, 3, 4));  // 10
  console.log(sum(1, 2, 3));     // 6
  return 0;
}
