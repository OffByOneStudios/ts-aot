// EXPECT-REJECT: 'arguments'
"use fast";
function bad(): number {
  return (arguments as any).length;
}
