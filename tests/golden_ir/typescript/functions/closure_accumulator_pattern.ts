// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: total: 55
// OUTPUT: items: a,b,c,d
// OUTPUT: log: [1] start [2] middle [3] end

// Test the benchmark-style pattern: closure accumulating results across
// multiple calls, combining mutable capture with data structure mutation.
// This mirrors how BenchmarkSuite.add() works in the benchmarks.

function user_main(): number {
  // Pattern 1: Numeric accumulator via closure
  let total = 0;
  const addTo = (n: number) => { total = total + n; };

  for (let i = 1; i <= 10; i++) {
    addTo(i);
  }
  console.log("total: " + total);

  // Pattern 2: Array accumulator via closure (like benchmark .add())
  const items: string[] = [];
  const collect = (s: string) => { items.push(s); };

  collect("a");
  collect("b");
  collect("c");
  collect("d");
  console.log("items: " + items.join(","));

  // Pattern 3: String builder via closure
  const parts: string[] = [];
  let counter = 0;
  const log = (msg: string) => {
    counter = counter + 1;
    parts.push("[" + counter + "] " + msg);
  };

  log("start");
  log("middle");
  log("end");
  console.log("log: " + parts.join(" "));

  return 0;
}
