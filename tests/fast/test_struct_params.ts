// "use fast" structs passed to and returned from functions.
"use fast";

struct Vec3 { x: f64; y: f64; z: f64; }

function dot(a: Vec3, b: Vec3): f64 {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

function make(x: f64, y: f64, z: f64): Vec3 {
  const v = new Vec3();
  v.x = x; v.y = y; v.z = z;
  return v;
}

function user_main(): number {
  let failed: i32 = 0;

  const a = make(1.0, 2.0, 3.0);
  const b = make(4.0, 5.0, 6.0);
  if (dot(a, b) !== 32.0) { console.log("FAIL dot " + dot(a, b)); failed = failed + 1; }

  const c = make(2.0, 0.0, 0.0);
  if (c.x !== 2.0) { console.log("FAIL make.x"); failed = failed + 1; }

  console.log("struct_params done, failed=" + failed);
  return failed > 0 ? 1 : 0;
}
