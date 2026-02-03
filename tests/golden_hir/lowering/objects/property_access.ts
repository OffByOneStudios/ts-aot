// Test: Object property access lowering to LLVM
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Object property operations lower to runtime calls
// HIR-CHECK: define @user_main() -> f64

// Create object and set properties
// Object property values optimized to i64 (set_prop.static boxes the value)
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: const.i64 10
// HIR-CHECK: set_prop.static {{.*}}, "x"
// HIR-CHECK: const.i64 20
// HIR-CHECK: set_prop.static {{.*}}, "y"

// Read properties
// HIR-CHECK: get_prop.static {{.*}}, "x"
// HIR-CHECK: get_prop.static {{.*}}, "y"

// HIR-CHECK: ret

// OUTPUT: 10
// OUTPUT: 20
// OUTPUT: 30

function user_main(): number {
  const point = { x: 10, y: 20 };
  console.log(point.x);
  console.log(point.y);
  console.log(point.x + point.y);
  return 0;
}
