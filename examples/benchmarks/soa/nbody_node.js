// SoA n-body benchmark — Node.js (V8) baseline.
//
// The SAME algorithm as nbody_dynamic.ts with the type annotations stripped,
// runnable directly by node. Plain number arrays: V8's JIT will promote them
// to PACKED_DOUBLE_ELEMENTS, so this is V8's optimized path for the same
// source style. Prints the identical checksum; run.py verifies and times it
// alongside the two ts-aot builds.

function user_main() {
  const N = 1024;
  const STEPS = 30;
  const dt = 0.01;

  const px = [];
  const py = [];
  const pz = [];
  const vx = [];
  const vy = [];
  const vz = [];
  const mass = [];

  for (let i = 0; i < N; i = i + 1) {
    px[i] = ((i * 13) % 100) / 10.0 - 5.0;
    py[i] = ((i * 7) % 100) / 10.0 - 5.0;
    pz[i] = ((i * 17) % 100) / 10.0 - 5.0;
    vx[i] = 0.0;
    vy[i] = 0.0;
    vz[i] = 0.0;
    mass[i] = 1.0;
  }

  for (let s = 0; s < STEPS; s = s + 1) {
    for (let i = 0; i < N; i = i + 1) {
      let ax = 0.0;
      let ay = 0.0;
      let az = 0.0;
      const xi = px[i];
      const yi = py[i];
      const zi = pz[i];
      for (let j = 0; j < N; j = j + 1) {
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
    for (let i = 0; i < N; i = i + 1) {
      px[i] = px[i] + vx[i] * dt;
      py[i] = py[i] + vy[i] * dt;
      pz[i] = pz[i] + vz[i] * dt;
    }
  }

  let sum = 0.0;
  for (let i = 0; i < N; i = i + 1) {
    sum = sum + px[i] + py[i] + pz[i];
  }
  console.log("nbody checksum=" + sum);
  return 0;
}

user_main();
