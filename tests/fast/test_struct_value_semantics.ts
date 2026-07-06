// "use fast" struct VALUE semantics: binding, assignment, argument, and return
// all COPY the struct (mutating the copy must not affect the original).
"use fast";

struct S { v: i32; }

function mutateParam(s: S): i32 {   // param must be an independent copy
  s.v = 777;
  return s.v;
}

function returnCopy(src: S): S {    // return an lvalue -> caller gets a copy
  return src;
}

function user_main(): number {
  let failed: i32 = 0;

  // 1. binding copy: const b = a
  const a = new S(); a.v = 10;
  const b = a;
  b.v = 99;
  if (a.v !== 10) { console.log("FAIL bind a.v=" + a.v); failed = failed + 1; }
  if (b.v !== 99) { console.log("FAIL bind b.v=" + b.v); failed = failed + 1; }

  // 2. struct-to-struct assignment copy: f = e
  const e = new S(); e.v = 3;
  let f = new S(); f.v = 4;
  f = e;
  f.v = 88;
  if (e.v !== 3)  { console.log("FAIL assign e.v=" + e.v); failed = failed + 1; }
  if (f.v !== 88) { console.log("FAIL assign f.v=" + f.v); failed = failed + 1; }

  // 3. argument copy: callee mutation must not affect caller
  const g = new S(); g.v = 20;
  const r = mutateParam(g);
  if (g.v !== 20)  { console.log("FAIL param g.v=" + g.v); failed = failed + 1; }
  if (r !== 777)   { console.log("FAIL param r=" + r); failed = failed + 1; }

  // 4. return copy: mutating the returned value must not affect the source
  const h = new S(); h.v = 50;
  const k = returnCopy(h);
  k.v = 61;
  if (h.v !== 50) { console.log("FAIL return h.v=" + h.v); failed = failed + 1; }
  if (k.v !== 61) { console.log("FAIL return k.v=" + k.v); failed = failed + 1; }

  console.log("value_semantics done, failed=" + failed);
  return failed > 0 ? 1 : 0;
}
