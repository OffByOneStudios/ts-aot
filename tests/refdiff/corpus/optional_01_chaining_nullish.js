var o = {a: {b: {c: 1}}};
console.log(o?.a?.b?.c);
console.log(o?.x?.y?.z);

var arr = [1, 2, 3];
console.log(arr?.[1]);
console.log(null?.[1]);

console.log(null ?? "fallback");
console.log(undefined ?? "fallback");
console.log(0 ?? "fallback");
console.log("" ?? "fallback");
console.log(false ?? "fallback");

var x = null;
x ??= 5;
console.log(x);
x ??= 10;
console.log(x);
