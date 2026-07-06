// EXPECT-REJECT: 'for...in'
"use fast";
function bad(o: i32): number {
  for (const k in (o as any)) { }
  return 0;
}
