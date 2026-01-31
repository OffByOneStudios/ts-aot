// Test: Logical operators in HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Logical operations generate boolean instructions
// HIR-CHECK: define @user_main() -> f64

// Boolean constants
// HIR-CHECK: const.bool true
// HIR-CHECK: const.bool false

// Logical AND
// HIR-CHECK: and.bool

// Logical OR
// HIR-CHECK: or.bool

// Logical NOT
// HIR-CHECK: not.bool

// HIR-CHECK: ret

// OUTPUT: false
// OUTPUT: true
// OUTPUT: false

function user_main(): number {
  const a: boolean = true;
  const b: boolean = false;

  // Logical AND - both must be true
  console.log(a && b);  // false

  // Logical OR - at least one true
  console.log(b || a);  // true

  // Logical NOT
  console.log(!a);  // false

  return 0;
}
