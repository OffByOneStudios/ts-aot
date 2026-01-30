// Test: Try-catch generates correct HIR for exception handling
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: try.begin
// HIR-CHECK: throw
// HIR-CHECK: try.end
// HIR-CHECK: catch.begin
// HIR-CHECK: catch.end
// HIR-CHECK: ret

// OUTPUT: caught: test error
// OUTPUT: finally executed

function user_main(): number {
  try {
    throw new Error("test error");
  } catch (e) {
    console.log("caught: " + (e as Error).message);
  } finally {
    console.log("finally executed");
  }
  return 0;
}
