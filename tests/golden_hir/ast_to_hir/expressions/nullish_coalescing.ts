// Test: Nullish coalescing (??) only uses fallback for null/undefined
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// Nullish coalescing creates conditional
// HIR-CHECK: call "ts_value_is_nullish"
// HIR-CHECK: ret

// OUTPUT: default
// OUTPUT: 0
// OUTPUT: hello

function user_main(): number {
  const a: string | null = null;
  const b: number | undefined = undefined;
  const c: string = "hello";

  // null ?? default = default
  console.log(a ?? "default");

  // undefined ?? default = default (but 0 is falsy, not nullish)
  const d: number = 0;
  console.log(d ?? 99);  // Should print 0, not 99

  // value ?? default = value
  console.log(c ?? "fallback");

  return 0;
}
