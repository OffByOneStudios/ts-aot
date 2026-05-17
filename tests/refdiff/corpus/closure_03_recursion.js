function fact(n) {
  return n <= 1 ? 1 : n * fact(n - 1);
}
console.log(fact(5));
console.log(fact(10));

function fib(n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}
console.log(fib(0), fib(1), fib(5), fib(10));

var ackermann = function(m, n) {
  if (m === 0) return n + 1;
  if (n === 0) return ackermann(m - 1, 1);
  return ackermann(m - 1, ackermann(m, n - 1));
};
console.log(ackermann(2, 3));
