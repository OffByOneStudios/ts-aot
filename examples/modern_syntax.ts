const a: any = null;
const b = a ?? "default";
console.log(b);

const obj: any = { x: 10 };
console.log(obj?.x);
console.log(obj?.y);

const nullObj: any = null;
console.log(nullObj?.x);
