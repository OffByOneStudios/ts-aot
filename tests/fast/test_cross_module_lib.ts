// Fast library module for test_cross_module: NativeArray types and unboxed
// numerics must flow through the module boundary.
"use fast";
export function fastSumSquares(n: i64): f64 {
  const arr = new NativeArray<f64>(n, Allocator.Temp);
  for (let i: i64 = 0; i < n; i++) { arr.set(i, i * 1.0); }
  let acc: f64 = 0;
  for (let i: i64 = 0; i < n; i++) {
    const x = arr.get(i);
    acc = acc + x * x;
  }
  return acc;
}
