// Simple Set array test
const s1 = new Set<number>();
s1.add(1);
s1.add(2);
console.log("s1.size:");
console.log(s1.size);

const arr: Set<number>[] = [];
arr.push(s1);
console.log("arr.length:");
console.log(arr.length);

const s2 = arr[0];
console.log("s2.size:");
console.log(s2.size);

console.log("s2.has(1):");
console.log(s2.has(1));
