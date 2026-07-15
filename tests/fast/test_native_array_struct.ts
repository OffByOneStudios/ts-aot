// NativeArray<struct> (AoS) + struct literals — the design doc §18 shape.
// Elements are struct payloads (numFields*8 bytes) memcpy'd in/out with
// VALUE semantics (get returns an independent copy). Struct literals lower
// by field NAME into the struct's own shape (order-independent).
"use fast";
struct Particle { x: f64; y: f64; vx: f64; vy: f64; }

function user_main(): number {
  let failed: i64 = 0;

  // struct literal, reverse field order (by-name lowering)
  const seed: Particle = { vy: 4, vx: 3, y: 2, x: 1 };
  if (seed.x !== 1 || seed.y !== 2 || seed.vx !== 3 || seed.vy !== 4) {
    console.log("FAIL literal " + seed.x + "," + seed.y + "," + seed.vx + "," + seed.vy);
    failed = failed + 1;
  }

  const p = new NativeArray<Particle>(8, Allocator.Temp);
  for (let i: i64 = 0; i < p.length; i++) {
    const q: Particle = { x: i * 1.0, y: 0, vx: 0.5, vy: 1.5 };
    p.set(i, q);
  }

  // step: q is a COPY; write back through set
  for (let i: i64 = 0; i < p.length; i++) {
    const q = p.get(i);
    q.x = q.x + q.vx;
    q.y = q.y + q.vy;
    p.set(i, q);
  }

  let sx: f64 = 0;
  let sy: f64 = 0;
  for (let i: i64 = 0; i < p.length; i++) {
    const q = p.get(i);
    sx = sx + q.x;
    sy = sy + q.y;
  }
  // sx = sum(i + 0.5) for i in 0..7 = 28 + 4 = 32; sy = 8 * 1.5 = 12
  if (sx !== 32) { console.log("FAIL sx=" + sx); failed = failed + 1; }
  if (sy !== 12) { console.log("FAIL sy=" + sy); failed = failed + 1; }

  // value semantics: mutating a copy must not touch the slot
  const a = p.get(0);
  a.x = 999;
  if (p.get(0).x === 999) { console.log("FAIL aliasing"); failed = failed + 1; }

  console.log("aos done, failed=" + failed);
  return failed > 0 ? 1 : 0;
}
