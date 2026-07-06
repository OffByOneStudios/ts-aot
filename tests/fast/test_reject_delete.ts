// EXPECT-REJECT: 'delete'
"use fast";
struct S { a: i32; }
function bad(s: S): number {
  delete (s as any).a;
  return 0;
}
