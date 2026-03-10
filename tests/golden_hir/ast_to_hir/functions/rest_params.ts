// Test: Rest parameters create array from call-site arguments
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// user_main packages arguments into arrays and calls sum_arr
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_array.boxed
// HIR-CHECK: set_elem
// HIR-CHECK: call "sum_arr"
// HIR-CHECK: ret

// sum function (monomorphized as sum_arr) iterates over rest array
// HIR-CHECK: define @sum_arr({{.*}}) -> f64
// HIR-CHECK: get_elem
// HIR-CHECK: add.f64
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
