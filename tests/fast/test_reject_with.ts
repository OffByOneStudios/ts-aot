// EXPECT-REJECT: 'with' statement
"use fast";
function bad(o: i32): number {
  with (o) { }
  return 0;
}
