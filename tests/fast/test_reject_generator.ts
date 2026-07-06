// EXPECT-REJECT: async/generator function
// EXPECT-REJECT: 'yield'
"use fast";
function* bad(): number {
  yield 1;
}
