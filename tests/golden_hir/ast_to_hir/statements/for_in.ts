// Test: For-in loop generates correct HIR control flow
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: set_prop.static {{.*}}, "a"
// HIR-CHECK: set_prop.static {{.*}}, "b"
// HIR-CHECK: set_prop.static {{.*}}, "c"
// HIR-CHECK: forin.body{{.*}}:
// HIR-CHECK: forin.end{{.*}}:
// HIR-CHECK: ret

// OUTPUT: a
// OUTPUT: b
// OUTPUT: c

function user_main(): number {
  const obj = { a: 1, b: 2, c: 3 };
  for (const key in obj) {
    console.log(key);
  }
  return 0;
}
