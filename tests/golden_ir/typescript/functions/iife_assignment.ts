// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_call_0
// CHECK-NOT: call {{.*}} @ts_value_get_object{{.*}}call {{.*}} @ts_value_get_object
// OUTPUT: 42

function user_main(): void {
  const result = (function() { 
    return { value: 42 }; 
  })();
  console.log(result.value);
}
