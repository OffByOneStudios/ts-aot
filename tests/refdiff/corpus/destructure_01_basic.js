var [a, b, c] = [1, 2, 3];
console.log(a, b, c);

var [x, , z] = [10, 20, 30];
console.log(x, z);

var [head, ...tail] = [1, 2, 3, 4];
console.log("head:", head, "tail:", tail.join(","));

var {p, q} = {p: 100, q: 200};
console.log(p, q);

var {r: renamed, s = 5} = {r: 1};
console.log(renamed, s);

var nested = {arr: [1, 2], inner: {v: "x"}};
var {arr: [first], inner: {v}} = nested;
console.log(first, v);
