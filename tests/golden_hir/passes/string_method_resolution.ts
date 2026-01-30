// Test: String method resolution pass resolves string methods to runtime calls
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// Test that string methods are resolved to ts_string_* calls
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "ts_string_toUpperCase"
// HIR-CHECK: call "ts_string_toLowerCase"
// HIR-CHECK: ret

// OUTPUT: HELLO WORLD
// OUTPUT: hello world

function user_main(): number {
  const str = "Hello World";

  // toUpperCase should resolve to ts_string_toUpperCase
  const upper = str.toUpperCase();
  console.log(upper);

  // toLowerCase should resolve to ts_string_toLowerCase
  const lower = str.toLowerCase();
  console.log(lower);

  return 0;
}
