// Test: Private class fields generate correct HIR
// RUN: %ts-aot %s --use-hir -o %t.exe && %t.exe

// HIR-CHECK: define @Counter_increment
// HIR-CHECK: get_prop.static %this, "#count"
// HIR-CHECK: set_prop.static %this, "#count"

// HIR-CHECK: define @Counter_getCount
// HIR-CHECK: get_prop.static %this, "#count"

// HIR-CHECK: define @user_main() -> f64
// HIR-CHECK: ret

// OUTPUT: 2

class Counter {
  #count: number = 0;

  increment(): void {
    this.#count++;
  }

  getCount(): number {
    return this.#count;
  }
}

function user_main(): number {
  const counter = new Counter();
  counter.increment();
  counter.increment();
  console.log(counter.getCount());
  return 0;
}
