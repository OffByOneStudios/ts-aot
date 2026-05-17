var nested = [1, [2, [3, [4]]]];
console.log("flat 1:", JSON.stringify(nested.flat()));
console.log("flat 2:", JSON.stringify(nested.flat(2)));
console.log("flat inf:", JSON.stringify(nested.flat(Infinity)));

var a = [1, 2, 3];
console.log("flatMap:", JSON.stringify(a.flatMap(x => [x, x * 10])));
