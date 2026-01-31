// Test: Getters and setters generate correct HIR
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: class @Temperature
// HIR-CHECK: mul.f64
// HIR-CHECK: add.f64
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64

// OUTPUT: 212
// OUTPUT: 0

class Temperature {
  private _celsius: number;

  constructor(c: number) {
    this._celsius = c;
  }

  get fahrenheit(): number {
    return this._celsius * 9 / 5 + 32;
  }

  set fahrenheit(f: number) {
    this._celsius = (f - 32) * 5 / 9;
  }
}

function user_main(): number {
  const t = new Temperature(100);
  console.log(t.fahrenheit);  // 212

  t.fahrenheit = 32;
  console.log(t._celsius);    // 0

  return 0;
}
