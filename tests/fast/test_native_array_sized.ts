// Sized NativeArray element slots: u8/i16/u32/f32 storage semantics
// (wrap on store, sign/zero-extend on load, f32 rounding), plus legacy
// f64/i64 slots. Also regression cover for the lowerPhi fragment-walk
// depth limit (many bounds checks in one block used to DROP a phi edge).
// No helper closure: FastCheck rejects captures — plain ifs.
"use fast";
function user_main(): number {
  let failed: i64 = 0;

  const b = new NativeArray<u8>(4, Allocator.Temp);
  b.set(0, 255); b.set(1, 256); b.set(2, 300); b[3] = 511;
  if (b.get(0) !== 255) { console.log("FAIL u8[0]=" + b.get(0)); failed = failed + 1; }
  if (b.get(1) !== 0)   { console.log("FAIL u8[1]=" + b.get(1)); failed = failed + 1; }  // wraps mod 256
  if (b.get(2) !== 44)  { console.log("FAIL u8[2]=" + b.get(2)); failed = failed + 1; }
  if (b[3] !== 255)     { console.log("FAIL u8[3]=" + b[3]); failed = failed + 1; }

  const s = new NativeArray<i16>(2, Allocator.Temp);
  s.set(0, -5); s.set(1, 40000);
  if (s.get(0) !== -5)     { console.log("FAIL i16[0]=" + s.get(0)); failed = failed + 1; }  // sign-extended
  if (s.get(1) !== -25536) { console.log("FAIL i16[1]=" + s.get(1)); failed = failed + 1; }  // wraps signed

  const u = new NativeArray<u32>(2, Allocator.Temp);
  u.set(0, 4294967295); u.set(1, 4294967296);
  if (u.get(0) !== 4294967295) { console.log("FAIL u32[0]=" + u.get(0)); failed = failed + 1; }  // zero-extended
  if (u.get(1) !== 0)          { console.log("FAIL u32[1]=" + u.get(1)); failed = failed + 1; }

  const f = new NativeArray<f32>(2, Allocator.Temp);
  f.set(0, 1.5); f.set(1, 0.1);
  if (f.get(0) !== 1.5) { console.log("FAIL f32[0]=" + f.get(0)); failed = failed + 1; }  // exact in f32
  const r = f.get(1);                                       // rounded to single precision
  if (!(r > 0.0999999 && r < 0.1000001 && r !== 0.1)) {
    console.log("FAIL f32 rounding r=" + r); failed = failed + 1;
  }

  const d = new NativeArray<f64>(1, Allocator.Temp);
  d.set(0, 0.1);
  if (d.get(0) !== 0.1) { console.log("FAIL f64 exact"); failed = failed + 1; }

  if (b.length !== 4) { console.log("FAIL u8.length"); failed = failed + 1; }
  if (s.length !== 2) { console.log("FAIL i16.length"); failed = failed + 1; }

  console.log("sized done, failed=" + failed);
  return failed > 0 ? 1 : 0;
}
