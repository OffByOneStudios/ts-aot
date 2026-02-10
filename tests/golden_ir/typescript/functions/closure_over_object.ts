// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: before: 1
// OUTPUT: after: 2
// OUTPUT: name: alice
// OUTPUT: arr: 1,2,3,4

// Test closures capturing and mutating objects and arrays.
// Exercises: object property mutation through captured reference,
// array push through captured reference, verifying shared state.

function user_main(): number {
  // Capture an object and mutate its properties
  const obj = { count: 1, name: "alice" };
  const increment = () => {
    obj.count = obj.count + 1;
  };

  console.log("before: " + obj.count);
  increment();
  console.log("after: " + obj.count);

  const getName = () => obj.name;
  console.log("name: " + getName());

  // Capture an array and push to it
  const arr: number[] = [1, 2, 3];
  const pushItem = (val: number) => {
    arr.push(val);
  };

  pushItem(4);
  console.log("arr: " + arr.join(","));

  return 0;
}
