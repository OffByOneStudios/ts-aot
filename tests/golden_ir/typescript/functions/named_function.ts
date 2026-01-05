// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @add
// CHECK: add i64
// OUTPUT: 7.000000

function add(a: number, b: number): number {
  return a + b;
}

function user_main(): void {
  const result = add(3, 4);
  console.log(result);
}
