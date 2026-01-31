// Test: Nested loops generate correct control flow
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: alloca
// HIR-CHECK: ret

// OUTPUT: 0,0
// OUTPUT: 0,1
// OUTPUT: 1,0
// OUTPUT: 1,1

function user_main(): number {
  for (let i: number = 0; i < 2; i++) {
    for (let j: number = 0; j < 2; j++) {
      console.log(i + "," + j);
    }
  }
  return 0;
}
