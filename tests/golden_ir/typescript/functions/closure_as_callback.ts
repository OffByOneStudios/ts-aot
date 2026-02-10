// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: mapped: 10,20,30
// OUTPUT: filtered: 20,30,40,50
// OUTPUT: reduced: 60
// OUTPUT: found: 30

// Test closures passed as callback arguments to array higher-order methods.
// Exercises: closure created inline as argument, captured variable used in
// predicate/transform, closure calling convention through ts_call_N.

function user_main(): number {
  const factor = 10;
  const arr: number[] = [1, 2, 3];

  // Closure as map callback - captures 'factor'
  const mapped = arr.map((x: number) => x * factor);
  console.log("mapped: " + mapped.join(","));

  // Closure as filter callback - captures 'threshold'
  const threshold = 15;
  const data: number[] = [10, 20, 30, 40, 50];
  const filtered = data.filter((x: number) => x > threshold);
  console.log("filtered: " + filtered.join(","));

  // Closure as reduce callback - captures 'factor'
  const reduced = arr.reduce((acc: number, x: number) => acc + x * factor, 0);
  console.log("reduced: " + reduced);

  // Closure as find callback - captures 'target'
  const target = 30;
  const found = data.find((x: number) => x === target);
  console.log("found: " + found);

  return 0;
}
