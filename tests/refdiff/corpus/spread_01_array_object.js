var a = [1, 2, 3];
var b = [...a, 4, 5];
console.log(b.join(","));

var c = [...a, ...b];
console.log(c.join(","));

function sum(...args) {
  return args.reduce((s, x) => s + x, 0);
}
console.log(sum(1, 2, 3, 4, 5));
console.log(sum(...a, ...b));

var o = {x: 1, y: 2};
var p = {...o, z: 3};
console.log(JSON.stringify(p));

var q = {a: 1, b: 2, ...{b: 99, c: 3}};
console.log(JSON.stringify(q));
