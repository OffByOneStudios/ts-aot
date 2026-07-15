// EXPECT-REJECT: reference-class allocation
"use fast";
class Widget {
  n: i32;
}
function user_main(): number {
  const w = new Widget();
  return 0;
}
