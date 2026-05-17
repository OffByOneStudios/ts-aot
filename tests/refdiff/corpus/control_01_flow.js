var n = 0;
for (var i = 0; i < 5; i++) n += i;
console.log("for-sum:", n);

var k = 10;
while (k > 0) { n += k; k -= 2; }
console.log("while-sum:", n);

var label = "";
outer: for (var i = 0; i < 3; i++) {
  for (var j = 0; j < 3; j++) {
    if (i === 1 && j === 1) break outer;
    label += i + "," + j + ";";
  }
}
console.log(label);

var x = 5;
switch (x) {
  case 1: console.log("one"); break;
  case 5: console.log("five"); /* fallthrough */
  case 6: console.log("five-or-six"); break;
  default: console.log("other");
}
