// EXPECT-REJECT: async/generator function
// EXPECT-REJECT: 'await'
"use fast";
async function bad(): Promise<number> {
  return await Promise.resolve(1);
}
