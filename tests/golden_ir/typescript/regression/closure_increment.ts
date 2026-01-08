// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_cell_create
// CHECK: call {{.*}} @ts_cell_get
// CHECK: call {{.*}} @ts_cell_set
// OUTPUT: 2,2,2,0

// This test verifies that ++ and -- operators work correctly in closures
// for captured mutable variables using cell-based storage.
// Related Node.js test: tests/node/compiler/closure_increment.ts

function user_main(): number {
  let count1 = 0;
  const fn1 = () => { count1 = count1 + 1; };  // Assignment pattern
  fn1();
  fn1();

  let count2 = 0;
  const fn2 = () => { count2++; };  // Postfix increment
  fn2();
  fn2();

  let count3 = 0;
  const fn3 = () => { ++count3; };  // Prefix increment
  fn3();
  fn3();

  let count4 = 2;
  const fn4 = () => { count4--; };  // Postfix decrement
  fn4();
  fn4();

  // All patterns should produce: 2,2,2,0
  console.log(count1 + "," + count2 + "," + count3 + "," + count4);
  return 0;
}
