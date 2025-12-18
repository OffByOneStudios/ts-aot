let t: [string, number] = ["hello", 42];
console.log(t[0]);
console.log(t[1]);

type Point = [number, number];
let p: Point = [10, 20];
console.log(p[0]);
console.log(p[1]);

let mixed: [string, number, boolean] = ["test", 123, true];
console.log(mixed[0]);
console.log(mixed[1]);
console.log(mixed[2]);
