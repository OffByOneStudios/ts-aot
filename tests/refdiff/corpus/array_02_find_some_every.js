var a = [1, 2, 3, 4, 5];
console.log("find > 3:", a.find(x => x > 3));
console.log("findIndex > 3:", a.findIndex(x => x > 3));
console.log("some > 4:", a.some(x => x > 4));
console.log("every > 0:", a.every(x => x > 0));
console.log("includes 3:", a.includes(3));
console.log("includes 99:", a.includes(99));
