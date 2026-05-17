var a = [1, 2, 3, 4, 5];
console.log("len:", a.length);
console.log("map:", a.map(x => x * 2).join(","));
console.log("filter:", a.filter(x => x % 2 === 0).join(","));
console.log("reduce:", a.reduce((s, x) => s + x, 0));
console.log("indexOf 3:", a.indexOf(3));
console.log("slice 1,3:", a.slice(1, 3).join(","));
console.log("reverse:", a.slice().reverse().join(","));
