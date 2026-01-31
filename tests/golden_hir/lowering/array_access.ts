// Test: Array element access lowering to LLVM
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Array access operations generate HIR instructions
// HIR-CHECK: define @user_main() -> f64

// Create array with elements
// HIR-CHECK: new_array.boxed
// HIR-CHECK: set_elem

// Get element by index
// HIR-CHECK: get_elem

// Set element by index
// HIR-CHECK: set_elem

// HIR-CHECK: ret

// OUTPUT: 10
// OUTPUT: 99
// OUTPUT: 30

function user_main(): number {
  const arr: number[] = [10, 20, 30];

  // Read first element
  console.log(arr[0]);  // 10

  // Modify second element
  arr[1] = 99;
  console.log(arr[1]);  // 99

  // Read last element
  console.log(arr[2]);  // 30

  return 0;
}
