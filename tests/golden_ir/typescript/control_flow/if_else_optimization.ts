// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: fcmp
// CHECK: br i1
// CHECK-NOT: call {{.*}} @ts_value_to_bool
// OUTPUT: x is greater than 5

function user_main(): void {
  const x = 10;
  if (x > 5) {
    console.log("x is greater than 5");
  } else {
    console.log("x is not greater than 5");
  }
}
