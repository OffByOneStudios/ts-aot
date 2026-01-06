const obj1 = { a: 1, b: 2 };
const obj2 = { ...obj1, c: 3 };
console.log(Object.keys(obj2).length);
for (const key of Object.keys(obj2)) {
    console.log(key);
}
