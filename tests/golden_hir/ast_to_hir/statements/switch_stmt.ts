// Test: Switch statement generates correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe
// XFAIL: HIR-to-LLVM lowering - switch on double with i64 case constants

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: switch
// HIR-CHECK: case
// HIR-CHECK: ret

// OUTPUT: two

function user_main(): number {
  const x: number = 2;
  switch (x) {
    case 1:
      console.log("one");
      break;
    case 2:
      console.log("two");
      break;
    default:
      console.log("other");
  }
  return 0;
}
