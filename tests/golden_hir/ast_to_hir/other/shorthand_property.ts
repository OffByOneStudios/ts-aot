// Test: Shorthand property names
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object
// HIR-CHECK: set_prop
// HIR-CHECK: get_prop
// HIR-CHECK: ret

// OUTPUT: 42
// OUTPUT: hello

function user_main(): number {
  const x = 42;
  const name = "hello";

  const obj = { x, name };

  console.log(obj.x);
  console.log(obj.name);

  return 0;
}
