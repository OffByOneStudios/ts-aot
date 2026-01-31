// Test: Method chaining with this return type
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// HIR-CHECK: class @Builder
// HIR-CHECK: define @Builder_setValue
// HIR-CHECK: set_prop.static
// HIR-CHECK: ret
// HIR-CHECK: define @Builder_multiply
// HIR-CHECK: get_prop.static
// HIR-CHECK: mul.f64
// HIR-CHECK: set_prop.static
// HIR-CHECK: ret

// HIR-CHECK: define @user_main() -> f64

// OUTPUT: 20

class Builder {
  value: number = 0;

  setValue(n: number): Builder {
    this.value = n;
    return this;
  }

  multiply(n: number): Builder {
    this.value = this.value * n;
    return this;
  }

  build(): number {
    return this.value;
  }
}

function user_main(): number {
  const result = new Builder()
    .setValue(5)
    .multiply(4)
    .build();

  console.log(result);  // 20
  return 0;
}
