// Test: Class expressions generate correct HIR
// XFAIL: Class expression methods not generated as separate HIR functions (falls back to dynamic dispatch)
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: call_method {{.*}}, "getValue"
// HIR-CHECK: call_method {{.*}}, "log"
// HIR-CHECK: ret

// OUTPUT: 42

const MyClass = class {
  value: number;

  constructor(val: number) {
    this.value = val;
  }

  getValue(): number {
    return this.value;
  }
};

function user_main(): number {
  const obj = new MyClass(42);
  console.log(obj.getValue());
  return 0;
}
