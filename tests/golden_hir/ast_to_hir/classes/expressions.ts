// Test: Class expressions generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object "__anon_class_0"
// HIR-CHECK: set_prop.static {{.*}}, "value"
// HIR-CHECK: call "ts_console_log"
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
