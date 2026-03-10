// Test: Optional chaining (?.) generates null checks
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Optional chaining creates nullish check + condbr branching
// HIR-CHECK: call "ts_value_is_nullish"
// HIR-CHECK: const.undefined
// HIR-CHECK: condbr
// HIR-CHECK: phi
// HIR-CHECK: ret

// OUTPUT: safe
// OUTPUT: undefined

function user_main(): number {
  const obj: any = null;
  const val = obj?.name;

  if (val === undefined) {
    console.log("safe");
  }
  console.log(val);
  return 0;
}
