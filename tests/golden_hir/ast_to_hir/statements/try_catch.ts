// Test: Try-catch generates correct HIR for exception handling
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: setup_try
// HIR-CHECK: throw
// HIR-CHECK: catch
// HIR-CHECK: get_exception
// HIR-CHECK: finally
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
