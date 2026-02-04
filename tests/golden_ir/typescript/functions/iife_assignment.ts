// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: 42

function user_main(): void {
  const result = (function() { 
    return { value: 42 }; 
  })();
  console.log(result.value);
}
