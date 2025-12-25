// BigInt tests
const b1 = 100n;
const b2 = BigInt(200);
const b3 = BigInt("300");

console.log(b1);
console.log(b2);
console.log(b3);
console.log(typeof b1);

// Symbol tests
const s1 = Symbol("desc");
const s2 = Symbol();
const s3 = Symbol.for("global");
const s4 = Symbol.for("global");

console.log(s1);
console.log(s2);
console.log(s3);
console.log(s3 === s4);
console.log(Symbol.keyFor(s3));
console.log(typeof s1);
