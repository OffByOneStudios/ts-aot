const obj1 = { nested: { value: 42 } };
const obj2 = { nested: undefined };
console.log(obj1.nested?.value);
console.log(obj2.nested?.value);
