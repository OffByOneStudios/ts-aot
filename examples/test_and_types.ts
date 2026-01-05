// Test && operator type inference
const a = true && global;
console.log('a typeof:', typeof a);
console.log('a === global:', a === global);

const b = global && true;
console.log('b typeof:', typeof b);
console.log('b === true:', b === true);
