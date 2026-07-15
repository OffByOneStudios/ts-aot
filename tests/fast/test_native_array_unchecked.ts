"use fast";

// getUnchecked / setUnchecked: the IN-LANGUAGE unsafe opt-out (Rust
// get_unchecked analog). No bounds check in any build mode — the caller
// vouches for the index. There is no compiler flag that removes checks;
// unsafe code is greppable at the call site.
// EXPECT-OUTPUT: PASS

function user_main(): number {
  let failed: i32 = 0;

  const a = new NativeArray<number>(4);
  a.setUnchecked(0, 1.5);
  a.setUnchecked(1, 2.5);
  a.set(2, 3.5);  // checked and unchecked interoperate on one array
  if (a.getUnchecked(0) !== 1.5) { console.log("FAIL u0"); failed = failed + 1; }
  if (a.get(1) !== 2.5) { console.log("FAIL c1"); failed = failed + 1; }
  if (a.getUnchecked(2) !== 3.5) { console.log("FAIL u2"); failed = failed + 1; }

  // arithmetic on unchecked reads stays unboxed
  a.setUnchecked(3, a.getUnchecked(0) + a.getUnchecked(1) * 2.0);
  if (a[3] !== 6.5) { console.log("FAIL u3=" + a[3]); failed = failed + 1; }

  const b = new NativeArray<i64>(2);
  b.setUnchecked(0, 21);
  if (b.getUnchecked(0) * 2 !== 42) { console.log("FAIL i64"); failed = failed + 1; }

  if (failed === 0) console.log("PASS");
  return failed;
}
