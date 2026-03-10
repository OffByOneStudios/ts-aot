// Test: Instance methods generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// Instance method with no parameters
// HIR-CHECK: define @Counter_getValue
// HIR-CHECK: get_prop.static {{.*}}, "value"
// HIR-CHECK: ret

// Instance method with parameters
// HIR-CHECK: define @Counter_add
// HIR-CHECK: get_prop.static {{.*}}, "value"
// HIR-CHECK: add.f64
// HIR-CHECK: set_prop.static {{.*}}, "value"
// HIR-CHECK: ret

// Method returning this for chaining
// HIR-CHECK: define @Counter_increment
// HIR-CHECK: get_prop.static {{.*}}, "value"
// HIR-CHECK: add.f64
// HIR-CHECK: set_prop.static {{.*}}, "value"
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: new_object_dynamic
// HIR-CHECK: set_prop.static {{.*}}, "value"
// HIR-CHECK: ret

// OUTPUT: 0
// OUTPUT: 10
// OUTPUT: 13

class Counter {
  value: number;

  constructor() {
    this.value = 0;
  }

  getValue(): number {
    return this.value;
  }

  add(amount: number): void {
    this.value = this.value + amount;
  }

  increment(): Counter {
    this.value = this.value + 1;
    return this;
  }
}

function user_main(): number {
  const counter = new Counter();
  console.log(counter.getValue());

  counter.add(10);
  console.log(counter.getValue());

  counter.increment().increment().increment();
  console.log(counter.getValue());

  return 0;
}
