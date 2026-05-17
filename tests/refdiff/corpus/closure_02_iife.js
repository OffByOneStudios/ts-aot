var result = (function(x) {
  return function(y) { return x + y; };
})(10);

console.log("11 + result:", result(11));
console.log("0 + result:", result(0));

// Loop with closures: classic captured-var issue
var fns = [];
for (let i = 0; i < 3; i++) {
  fns.push(function() { return i; });
}
console.log(fns.map(f => f()).join(","));
