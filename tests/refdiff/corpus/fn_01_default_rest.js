function greet(name = "world", greeting = "hello") {
  return greeting + ", " + name + "!";
}
console.log(greet());
console.log(greet("bob"));
console.log(greet("alice", "hi"));

function sum(first, ...rest) {
  return first + rest.reduce((a, b) => a + b, 0);
}
console.log(sum(1));
console.log(sum(1, 2, 3, 4, 5));

var arrow = (a, b = 5) => a + b;
console.log(arrow(10));
console.log(arrow(10, 20));
