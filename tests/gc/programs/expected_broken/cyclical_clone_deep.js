// Distilled from lodash test.js `_.cloneDeep should deep clone objects with
// lots of circular references` (line 2759). The lodash form crashes the
// upstream harness immediately under default nursery; this is the smallest
// self-contained workload that reliably triggers the SAME moving-GC
// corruption — many small object literals + array cells + cycle-detection
// cache, with forced minor GCs in the middle of the deep recursion.
//
// EXPECTED RESULT today (commit before the GC fix):
//   default nursery   : crash (exit != 0)
//   TS_GC_NURSERY=0   : PASS
//   TS_GC_VERIFY=2    : either pass or INV-1 abort
// The differential mismatch IS the test signal. Once the moving-GC fix
// lands, all three configs should produce identical "PASS" and the file
// can be moved out of expected_broken/ back into programs/.
//
// Shape:
//   cyclical = { v0: [cyclical, {idx:0,tag:'s0'}],
//                v1: [cyclical.v0, {idx:1,tag:'s1'}],
//                ...
//                vN: [cyclical.v(N-1), {idx:N,tag:'sN'}] }
//   (a long chain plus a backward cycle at v0)

function cloneDeep(value, seen, depth) {
  if (value === null || typeof value !== 'object') return value;
  if (seen.has(value)) return seen.get(value);
  // Force a minor GC every 32 frames — this is what surfaces the bug
  // (many references held across collection points; nothing pinning them).
  if ((depth & 31) === 0) __ts_gc_minor();
  var out;
  if (Array.isArray(value)) {
    out = [];
    seen.set(value, out);
    for (var i = 0; i < value.length; i++) out.push(cloneDeep(value[i], seen, depth + 1));
  } else {
    out = {};
    seen.set(value, out);
    var keys = Object.keys(value);
    for (var j = 0; j < keys.length; j++) {
      out[keys[j]] = cloneDeep(value[keys[j]], seen, depth + 1);
    }
  }
  return out;
}

function user_main() {
  var N = 500;
  var cyclical = {};
  for (var i = 0; i <= N; i++) {
    cyclical['v' + i] = [
      i ? cyclical['v' + (i - 1)] : cyclical,
      { idx: i, tag: 's' + i }
    ];
  }

  var clone = cloneDeep(cyclical, new Map(), 0);

  // Structural invariants the clone must preserve (lodash test asserts the
  // first two; we add a metadata-tag check to catch silent field corruption).
  var last = clone['v' + N];
  var prev = clone['v' + (N - 1)];
  if (!last) { console.log("FAIL: clone['v' + N] missing"); return 1; }
  if (!last[1]) { console.log("FAIL: clone meta object missing"); return 1; }
  if (last[1].idx !== N) { console.log("FAIL: meta.idx=" + last[1].idx + " expected " + N); return 1; }
  if (last[1].tag !== ('s' + N)) { console.log("FAIL: meta.tag corrupt"); return 1; }
  if (last[0] !== prev) { console.log("FAIL: chain link not preserved"); return 1; }
  if (last === cyclical['v' + N]) { console.log("FAIL: aliased original"); return 1; }

  console.log("PASS");
  return 0;
}
