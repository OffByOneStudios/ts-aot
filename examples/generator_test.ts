function* simpleGen() {
    console.log("Generator: yield 1");
    yield 1;
    console.log("Generator: yield 2");
    yield 2;
    console.log("Generator: return 3");
    return 3;
}

const g = simpleGen();
console.log("Main: calling next 1");
let r1 = g.next();
console.log(r1.value);
console.log(r1.done);

console.log("Main: calling next 2");
let r2 = g.next();
console.log(r2.value);
console.log(r2.done);

console.log("Main: calling next 3");
let r3 = g.next();
console.log(r3.value);
console.log(r3.done);
