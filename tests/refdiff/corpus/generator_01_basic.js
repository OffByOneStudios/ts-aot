function* range(start, end) {
  for (var i = start; i < end; i++) yield i;
}

var result = [];
for (var v of range(1, 5)) result.push(v);
console.log(result.join(","));

function* inner() { yield "a"; yield "b"; }
function* outer() { yield 1; yield* inner(); yield 2; }
console.log([...outer()].join(","));

function* echo() {
  var got = yield 1;
  yield got * 10;
}
var g = echo();
console.log(g.next().value);
console.log(g.next(7).value);
console.log(g.next().done);
