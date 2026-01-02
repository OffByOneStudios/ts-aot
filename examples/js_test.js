function add(a, b) {
    return a + b;
}

let x = add(1, 2);
console.log("1 + 2 =", x);

let obj = { a: 10 };
obj.b = 20;
console.log("obj.a + obj.b =", obj.a + obj.b);

if ("a" in obj) {
    console.log("obj has a");
}

delete obj.a;
if (!("a" in obj)) {
    console.log("obj does not have a anymore");
}

console.log("typeof x =", typeof x);
console.log("typeof obj =", typeof obj);

let keys = Object.keys(obj);
console.log("keys =", keys);
