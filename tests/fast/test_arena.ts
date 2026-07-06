"use fast";

// Each call allocates a Temp array, fills it, returns a scalar. The array is
// bulk-freed when this frame's ts_native_arena_release runs on return — so
// calling this 200k times must not grow memory without bound.
function work(n: i64): number {
  const a = new NativeArray<number>(64, Allocator.Temp);
  let s: number = 0;
  for (let i = 0; i < 64; i = i + 1) {
    a.set(i, i * n);
    s = s + a.get(i);
  }
  return s;
}

function user_main(): number {
  let total: number = 0;
  for (let k = 0; k < 200000; k = k + 1) {
    total = total + work(k % 3);
  }

  // Persistent alloc/dispose churn (each freed immediately).
  for (let k = 0; k < 100000; k = k + 1) {
    const p = new NativeArray<i64>(8, Allocator.Persistent);
    p.set(0, k);
    p.dispose();
  }

  console.log("arena_stress total=" + total);
  return 0;
}
// EXPECT-OUTPUT: arena_stress total=403197984
