"use fast";

function user_main(): number {
  let failed: i32 = 0;

  // f64 NativeArray
  const a = new NativeArray<number>(4);
  if (a.length !== 4) { console.log("FAIL length=" + a.length); failed = failed + 1; }
  a.set(0, 1.5);
  a.set(1, 2.5);
  a.set(3, 9.25);
  if (a.get(0) !== 1.5) { console.log("FAIL a0=" + a.get(0)); failed = failed + 1; }
  if (a.get(1) !== 2.5) { console.log("FAIL a1=" + a.get(1)); failed = failed + 1; }
  if (a.get(2) !== 0) { console.log("FAIL a2=" + a.get(2)); failed = failed + 1; }
  if (a.get(3) !== 9.25) { console.log("FAIL a3=" + a.get(3)); failed = failed + 1; }

  // sum loop over length
  let sum: number = 0;
  for (let i = 0; i < a.length; i = i + 1) {
    sum = sum + a.get(i);
  }
  if (sum !== 13.25) { console.log("FAIL sum=" + sum); failed = failed + 1; }

  // i64 NativeArray
  const b = new NativeArray<i64>(3, Allocator.Persistent);
  b.set(0, 100);
  b.set(2, 300);
  if (b.get(0) !== 100) { console.log("FAIL b0=" + b.get(0)); failed = failed + 1; }
  if (b.get(2) !== 300) { console.log("FAIL b2=" + b.get(2)); failed = failed + 1; }
  b.dispose();

  console.log("na done, failed=" + failed);
  return failed > 0 ? 1 : 0;
}
