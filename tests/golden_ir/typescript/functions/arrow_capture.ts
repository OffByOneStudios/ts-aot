// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__arrow_fn
// OUTPUT: 10

function user_main(): void {
  const fn = () => 10;
  console.log(fn());
}
