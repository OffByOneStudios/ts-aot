// EXPECT-REJECT: eval()
"use fast";
function bad(): number {
  return eval("1 + 1");
}
