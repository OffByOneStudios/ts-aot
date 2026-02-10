// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: add: 30
// OUTPUT: greet: Hello, Alice!
// OUTPUT: greet: Hello, Bob!

// Test closures returning closures (currying / partial application).
// Exercises: nested MakeClosure, outer capture propagated through
// inner closure, multi-level trampoline chain.

function add(a: number) {
  return (b: number) => a + b;
}

function greeter(greeting: string) {
  return (name: string) => greeting + ", " + name + "!";
}

function user_main(): number {
  const add10 = add(10);
  const result = add10(20);
  console.log("add: " + result);

  const hello = greeter("Hello");
  console.log("greet: " + hello("Alice"));
  console.log("greet: " + hello("Bob"));

  return 0;
}
