// Test: Class expressions generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: call "ts_new_from_constructor_1"
// HIR-CHECK: call_method {{.*}}, "getValue"
// HIR-CHECK: call "ts_console_log"
// HIR-CHECK: ret

// OUTPUT: 0

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
