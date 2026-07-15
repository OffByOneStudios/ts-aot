// EXPECT-REJECT: capturing closure
"use fast";
function outer(scale: number): number {
  const inner = function (x: number): number {
    return x * scale;
  };
  return inner(2.0);
}
