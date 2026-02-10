// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// CHECK: @ts_closure_init_capture
// OUTPUT: sum: 60
// OUTPUT: concat: hello world 42

// Test closure capturing multiple variables of different types simultaneously.
// Exercises: multi-capture init, type-diverse boxing (int, string, number),
// unboxing each capture with correct type dispatch in LoadCapture.

function user_main(): number {
  const a: number = 10;
  const b: number = 20;
  const c: number = 30;

  const sumThree = () => a + b + c;
  console.log("sum: " + sumThree());

  const str: string = "hello";
  const str2: string = "world";
  const num: number = 42;

  const concatAll = () => str + " " + str2 + " " + num;
  console.log("concat: " + concatAll());

  return 0;
}
