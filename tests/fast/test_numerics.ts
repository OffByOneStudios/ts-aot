// "use fast" fixed-width numeric types compile via unboxed paths and compute
// correctly. Positive test: exits 0 on success.
"use fast";

function addI32(a: i32, b: i32): i32 { return a + b; }
function subI64(a: i64, b: i64): i64 { return a - b; }
function mulF64(a: f64, b: f64): f64 { return a * b; }
function mulF32(a: f32, b: f32): f32 { return a * b; }
function widths(a: i8, b: i16, c: u32, d: u8): i64 { return a + b + c + d; }

function user_main(): number {
  let failed: i32 = 0;
  if (addI32(20, 22) !== 42) { console.log("FAIL addI32"); failed = failed + 1; }
  if (subI64(100, 58) !== 42) { console.log("FAIL subI64"); failed = failed + 1; }
  if (mulF64(3.0, 4.0) !== 12.0) { console.log("FAIL mulF64"); failed = failed + 1; }
  if (mulF32(2.0, 5.0) !== 10.0) { console.log("FAIL mulF32"); failed = failed + 1; }
  if (widths(1, 2, 3, 4) !== 10) { console.log("FAIL widths"); failed = failed + 1; }

  let total: i64 = 0;
  for (let i: i32 = 0; i < 100; i++) { total = total + i; }
  if (total !== 4950) { console.log("FAIL loopSum " + total); failed = failed + 1; }

  console.log("numerics done, failed=" + failed);
  return failed > 0 ? 1 : 0;
}
