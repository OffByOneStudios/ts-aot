// EXPECT-REJECT: parameter typed 'any'
// EXPECT-REJECT: variable typed 'any'
"use fast";
function bad(x: any): number {
  const y: any = 1;
  return x + y;
}
