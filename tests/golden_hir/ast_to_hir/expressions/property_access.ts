// Test: Property access generates correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: const.f64 42
// HIR-CHECK: set_prop.static {{.*}}, "value"
// HIR-CHECK: get_prop.static {{.*}}, "value"
// HIR-CHECK: ret

// OUTPUT: 42

function user_main(): number {
  const obj = { value: 42 };
  console.log(obj.value);
  return 0;
}
