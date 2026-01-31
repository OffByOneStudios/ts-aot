// Test: Rest parameters create array
// XFAIL: Rest parameters not supported in HIR pipeline
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @sum
// HIR-CHECK: alloca
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
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
