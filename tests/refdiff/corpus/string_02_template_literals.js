var name = "world";
var n = 42;
console.log(`hello, ${name}!`);
console.log(`n = ${n}, n*2 = ${n * 2}`);
console.log(`multi
line
string`);
function tag(strings, ...values) {
  return strings.map((s, i) => s + (i < values.length ? "[" + values[i] + "]" : "")).join("");
}
console.log(tag`a${1}b${2}c`);
