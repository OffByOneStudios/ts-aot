const obj = { a: 1, b: 2, c: 3 };
for (const k in obj) {
    console.log(k);
}

const map = new Map<string, number>();
map.set("x", 10);
map.set("y", 20);
for (const k in map) {
    console.log(k);
}
