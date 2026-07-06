// SoA n-body benchmark — dynamic (baseline) path.
//
// The SAME algorithm as nbody_fast.ts, but over ordinary dynamic number[]
// arrays and without the "use fast" directive. Element access goes through the
// managed-array path (bounds/box-aware) rather than a contiguous unboxed load.
// Both print the identical checksum; run.py times the pair and reports the
// delta.

function user_main(): number {
  const N: number = 1024;
  const STEPS: number = 30;
  const dt: number = 0.01;

  const px: number[] = [];
  const py: number[] = [];
  const pz: number[] = [];
  const vx: number[] = [];
  const vy: number[] = [];
  const vz: number[] = [];
  const mass: number[] = [];

  for (let i: number = 0; i < N; i = i + 1) {
    px[i] = ((i * 13) % 100) / 10.0 - 5.0;
    py[i] = ((i * 7) % 100) / 10.0 - 5.0;
    pz[i] = ((i * 17) % 100) / 10.0 - 5.0;
    vx[i] = 0.0;
    vy[i] = 0.0;
    vz[i] = 0.0;
    mass[i] = 1.0;
  }

  for (let s: number = 0; s < STEPS; s = s + 1) {
    for (let i: number = 0; i < N; i = i + 1) {
      let ax: number = 0.0;
      let ay: number = 0.0;
      let az: number = 0.0;
      const xi = px[i];
      const yi = py[i];
      const zi = pz[i];
      for (let j: number = 0; j < N; j = j + 1) {
        const dx = px[j] - xi;
        const dy = py[j] - yi;
        const dz = pz[j] - zi;
        const d2 = dx * dx + dy * dy + dz * dz + 1e-9;
        const inv = 1.0 / Math.sqrt(d2);
        const f = mass[j] * inv * inv * inv;
        ax = ax + dx * f;
        ay = ay + dy * f;
        az = az + dz * f;
      }
      vx[i] = vx[i] + ax * dt;
      vy[i] = vy[i] + ay * dt;
      vz[i] = vz[i] + az * dt;
    }
    for (let i: number = 0; i < N; i = i + 1) {
      px[i] = px[i] + vx[i] * dt;
      py[i] = py[i] + vy[i] * dt;
      pz[i] = pz[i] + vz[i] * dt;
    }
  }

  let sum: number = 0.0;
  for (let i: number = 0; i < N; i = i + 1) {
    sum = sum + px[i] + py[i] + pz[i];
  }
  console.log("nbody checksum=" + sum);
  return 0;
}
