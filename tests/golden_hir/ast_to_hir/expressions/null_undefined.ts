// Test: Null and undefined literals generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: const.null
// HIR-CHECK: const.undefined
// HIR-CHECK: ret

// OUTPUT: null
// OUTPUT: undefined
// OUTPUT: true
// OUTPUT: true

function user_main(): number {
  const a: null = null;
  const b: undefined = undefined;

  console.log(a);
  console.log(b);
  console.log(a === null);
  console.log(b === undefined);

  return 0;
}
