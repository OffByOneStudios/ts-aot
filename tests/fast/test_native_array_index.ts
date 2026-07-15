"use fast";

// NativeArray element-access SUGAR: arr[i] reads and arr[i] = v writes must
// behave exactly like .get(i) / .set(i, v) — inline unboxed slot access,
// unboxed element result type (no re-boxing of downstream arithmetic), for
// both f64 and i64 element kinds, and interop with the method forms on the
// same array.

function user_main(): number {
  let failed: i32 = 0;

  // f64: sugar writes read back through both forms
  const a = new NativeArray<number>(4);
  a[0] = 1.5;
  a[1] = 2.5;
  a.set(2, 3.5);
  if (a[0] !== 1.5) { console.log("FAIL a[0]=" + a[0]); failed = failed + 1; }
  if (a.get(1) !== 2.5) { console.log("FAIL get(1)=" + a.get(1)); failed = failed + 1; }
  if (a[2] !== 3.5) { console.log("FAIL a[2]=" + a[2]); failed = failed + 1; }

  // arithmetic on sugar reads stays unboxed and correct
  a[3] = a[0] + a[1] * 2.0;
  if (a[3] !== 6.5) { console.log("FAIL a[3]=" + a[3]); failed = failed + 1; }

  // i64 element kind
  const b = new NativeArray<i64>(3);
  b[0] = 10;
  b[1] = 20;
  b[2] = b[0] + b[1];
  if (b[2] !== 30) { console.log("FAIL b[2]=" + b[2]); failed = failed + 1; }

  // loop with variable index (the SoA access pattern)
  const f = new NativeArray<number>(8);
  for (let i: i64 = 0; i < 8; i = i + 1) {
    f[i] = i * 1.5;
  }
  let s: number = 0.0;
  for (let i: i64 = 0; i < 8; i = i + 1) {
    s = s + f[i];
  }
  if (s !== 42.0) { console.log("FAIL sum=" + s); failed = failed + 1; }

  if (failed === 0) console.log("PASS");
  return failed;
}
