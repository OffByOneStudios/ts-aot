// SoA n-body benchmark — "use fast" path.
//
// Structure-of-Arrays: each component (position/velocity/mass) is a separate
// contiguous NativeArray of unboxed f64 slots. The O(N^2) force loop reads
// them linearly, which the backend can keep in registers and vectorize — this
// is the data-oriented layout the "use fast" subset is built for
// (docs/design/use-fast.md §13). Compare against nbody_dynamic.ts (the same
// algorithm over dynamic number[] arrays) with run.py.
"use fast";

function user_main(): number {
  const N: i64 = 1024;
  const STEPS: i64 = 30;
  const dt: number = 0.01;

  // SoA component arrays. Temp: bulk-freed when user_main's frame exits.
  const px = new NativeArray<number>(N, Allocator.Temp);
  const py = new NativeArray<number>(N, Allocator.Temp);
  const pz = new NativeArray<number>(N, Allocator.Temp);
  const vx = new NativeArray<number>(N, Allocator.Temp);
  const vy = new NativeArray<number>(N, Allocator.Temp);
  const vz = new NativeArray<number>(N, Allocator.Temp);
  const mass = new NativeArray<number>(N, Allocator.Temp);

  // Deterministic init — small exact-integer arithmetic so the checksum
  // matches the dynamic version bit-for-bit.
  for (let i: i64 = 0; i < N; i = i + 1) {
    px.set(i, ((i * 13) % 100) / 10.0 - 5.0);
    py.set(i, ((i * 7) % 100) / 10.0 - 5.0);
    pz.set(i, ((i * 17) % 100) / 10.0 - 5.0);
    vx.set(i, 0.0);
    vy.set(i, 0.0);
    vz.set(i, 0.0);
    mass.set(i, 1.0);
  }

  for (let s: i64 = 0; s < STEPS; s = s + 1) {
    for (let i: i64 = 0; i < N; i = i + 1) {
      let ax: number = 0.0;
      let ay: number = 0.0;
      let az: number = 0.0;
      const xi = px.get(i);
      const yi = py.get(i);
      const zi = pz.get(i);
      for (let j: i64 = 0; j < N; j = j + 1) {
        const dx = px.get(j) - xi;
        const dy = py.get(j) - yi;
        const dz = pz.get(j) - zi;
        const d2 = dx * dx + dy * dy + dz * dz + 1e-9;
        const inv = 1.0 / Math.sqrt(d2);
        const f = mass.get(j) * inv * inv * inv;
        ax = ax + dx * f;
        ay = ay + dy * f;
        az = az + dz * f;
      }
      vx.set(i, vx.get(i) + ax * dt);
      vy.set(i, vy.get(i) + ay * dt);
      vz.set(i, vz.get(i) + az * dt);
    }
    for (let i: i64 = 0; i < N; i = i + 1) {
      px.set(i, px.get(i) + vx.get(i) * dt);
      py.set(i, py.get(i) + vy.get(i) * dt);
      pz.set(i, pz.get(i) + vz.get(i) * dt);
    }
  }

  let sum: number = 0.0;
  for (let i: i64 = 0; i < N; i = i + 1) {
    sum = sum + px.get(i) + py.get(i) + pz.get(i);
  }
  console.log("nbody checksum=" + sum);
  return 0;
}
