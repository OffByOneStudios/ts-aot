// Test: Boolean operators generate correct HIR opcodes
// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.bool true
// HIR-CHECK: const.bool false
// HIR-CHECK: and.bool
// HIR-CHECK: or.bool
// HIR-CHECK: not.bool
// HIR-CHECK: ret

// OUTPUT: false
// OUTPUT: true
// OUTPUT: false

function user_main(): number {
  const a: boolean = true;
  const b: boolean = false;

  console.log(a && b);  // false - and.bool
  console.log(a || b);  // true - or.bool
  console.log(!a);      // false - not.bool

  return 0;
}
