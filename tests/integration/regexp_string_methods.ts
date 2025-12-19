const s = "hello world";
console.log("Match:");
const m = s.match(/hello/);
if (m) {
    console.log(m[0]);
}

console.log("\nReplace:");
console.log(s.replace(/world/, "TypeScript"));

console.log("\nSplit:");
const parts = s.split(/ /);
for (const p of parts) {
    console.log(p);
}

console.log("\nReplace with global:");
const s2 = "abc abc abc";
console.log(s2.replace(/abc/g, "def"));
