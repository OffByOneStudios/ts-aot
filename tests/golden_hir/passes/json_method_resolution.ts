// Test: JSON method resolution
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// JSON.stringify should be resolved to builtin
// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: {"x":10}
// OUTPUT: 10

function user_main(): number {
  const obj = { x: 10 };
  const json = JSON.stringify(obj);
  console.log(json);

  const parsed = JSON.parse(json);
  console.log(parsed.x);

  return 0;
}
