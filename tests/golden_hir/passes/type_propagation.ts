// Test: Type propagation pass propagates types through operations
// RUN: %ts-aot %s --use-hir --dump-hir -o %t.exe && %t.exe

// Type propagation ensures operations use correct types
// HIR-CHECK: define @user_main() -> f64

// Number operations should use f64
// HIR-CHECK: const.f64 5.000000
// HIR-CHECK: const.f64 10.000000
// HIR-CHECK: add.f64

// String operations should use string type
// HIR-CHECK: const.string "hello"
// HIR-CHECK: const.string "world"
// HIR-CHECK: string.concat

// Boolean operations
// HIR-CHECK: const.bool true
// HIR-CHECK: not.bool

// HIR-CHECK: ret

// OUTPUT: 15
// OUTPUT: helloworld
// OUTPUT: false

function user_main(): number {
  // Number type propagation
  const a: number = 5;
  const b: number = 10;
  const sum = a + b;
  console.log(sum);

  // String type propagation
  const s1: string = "hello";
  const s2: string = "world";
  const concat = s1 + s2;
  console.log(concat);

  // Boolean type propagation
  const t: boolean = true;
  const notT = !t;
  console.log(notT);

  return 0;
}
