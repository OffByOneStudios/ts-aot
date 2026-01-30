// Test: Regular expressions generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "ts_regexp_from_literal"
// HIR-CHECK: call_method {{.*}}, "test"
// HIR-CHECK: condbr
// HIR-CHECK: ret

// OUTPUT: matched

function user_main(): number {
  const re = /hello/;
  const result = re.test("hello world");

  if (result) {
    console.log("matched");
  } else {
    console.log("no match");
  }

  return 0;
}
