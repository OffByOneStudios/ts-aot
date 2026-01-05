// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @lambda_
// OUTPUT: 10

function user_main(): void {
  const fn = () => 10;
  console.log(fn());
}
