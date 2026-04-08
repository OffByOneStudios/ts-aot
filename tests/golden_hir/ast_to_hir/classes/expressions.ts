// Test: Class expressions generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// Phase 9c-i: previously this test asserted OUTPUT: 0 and a HIR-CHECK for
// "ts_new_from_constructor_1". That was locking in a codegen bug — class
// expressions weren't pre-registered in pass 1, so visitNewExpression for
// `new MyClass(42)` couldn't find the class and fell through to the runtime
// constructor dispatcher with an undefined constructor. The runtime stored
// arg1 as `.message` instead of running the actual constructor body.
// Updated to assert the correct behavior.
//
// After 9c-i, class expressions take the same fast path as class
// declarations: the constructor is inlined as `new_object_dynamic` +
// `set_prop.static`, and getValue() is inlined to a direct
// `get_prop.static` (no method dispatch needed because the analyzer knows
// the receiver type and the method body is trivial).

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object_dynamic
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
