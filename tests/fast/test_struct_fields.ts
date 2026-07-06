// "use fast" struct value types: fixed-shape unboxed fields (f64 + i32),
// read/write, mixed field types. Positive test.
"use fast";

struct Vec3 { x: f64; y: f64; z: f64; }
struct Mixed { count: i32; scale: f64; }

function user_main(): number {
  let failed: i32 = 0;

  const v = new Vec3();
  v.x = 1.5; v.y = 2.5; v.z = 3.0;
  if (v.x + v.y + v.z !== 7.0) { console.log("FAIL vec3 sum"); failed = failed + 1; }
  v.x = v.x * 2.0;
  if (v.x !== 3.0) { console.log("FAIL vec3 mutate"); failed = failed + 1; }

  const m = new Mixed();
  m.count = 5; m.scale = 2.0;
  if (m.count !== 5) { console.log("FAIL mixed.count"); failed = failed + 1; }
  if (m.scale !== 2.0) { console.log("FAIL mixed.scale"); failed = failed + 1; }
  m.count = m.count + 3;
  if (m.count !== 8) { console.log("FAIL mixed.count mutate"); failed = failed + 1; }

  console.log("struct_fields done, failed=" + failed);
  return failed > 0 ? 1 : 0;
}
