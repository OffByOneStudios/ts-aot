// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: 0
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3
// OUTPUT: after reset: 0

// Test multiple closures sharing one mutable captured variable (module pattern).
// Exercises: shared TsCell between closures, mutation via StoreCapture
// from different closures, object literal with closure-valued properties.

function makeCounter() {
  let value = 0;
  const get = () => value;
  const increment = () => { value = value + 1; };
  const reset = () => { value = 0; };
  return { get, increment, reset };
}

function user_main(): number {
  const counter = makeCounter();
  console.log(counter.get());
  counter.increment();
  console.log(counter.get());
  counter.increment();
  console.log(counter.get());
  counter.increment();
  console.log(counter.get());
  counter.reset();
  console.log("after reset: " + counter.get());
  return 0;
}
