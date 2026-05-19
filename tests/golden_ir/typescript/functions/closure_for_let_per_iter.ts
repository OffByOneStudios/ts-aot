// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2

// ECMA-262 14.7.4.4 CreatePerIterationEnvironment: each iteration of
// `for (let i ...)` creates a fresh lexical binding. Closures created in
// the body capture THAT iteration's binding, so each one sees the value
// of `i` at the moment the body ran for its iteration.
//
// This test covers the direct capture form (without the `const idx = i`
// workaround that closure_in_loop.ts uses).

function user_main(): number {
  const fns: Array<() => number> = [];

  for (let i = 0; i < 3; i++) {
    fns.push(() => i);
  }

  for (let j = 0; j < 3; j++) {
    console.log(fns[j]());
  }

  return 0;
}
