const obj = {
    a: 1,
    b: "hello",
    c: [1, 2, 3],
    d: { e: 4 },
    f: /abc/g,
    g: new Date(1600000000000),
    h: undefined
};

console.log("Full object:");
console.log(JSON.stringify(obj));

console.log("\nPretty printed (2 spaces):");
console.log(JSON.stringify(obj, null, 2));

console.log("\nFiltered (only a and b):");
console.log(JSON.stringify(obj, ["a", "b"]));

console.log("\nFiltered and pretty printed:");
console.log(JSON.stringify(obj, ["a", "c"], 4));

const arr = [1, undefined, 3];
console.log("\nArray with undefined:");
console.log(JSON.stringify(arr));
