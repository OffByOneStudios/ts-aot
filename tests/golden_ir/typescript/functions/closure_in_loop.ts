// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3
// OUTPUT: 4

// Test closures created inside a loop, each capturing a distinct loop variable.
// Exercises: per-iteration closure creation, each closure gets its own cell
// with the loop variable's value at creation time (not a shared reference).
// This is the classic "closure in a loop" problem.

function user_main(): number {
  const fns: Array<() => number> = [];

  for (let i = 0; i < 5; i++) {
    const idx = i;
    fns.push(() => idx);
  }

  // Each closure should return its own captured value
  for (let j = 0; j < 5; j++) {
    console.log(fns[j]());
  }

  return 0;
}
