// Test global object detection
console.log('typeof global:', typeof global);
console.log('global exists:', global !== undefined);

// Test property access on global
const objFromGlobal = global.Object;
console.log('objFromGlobal from global.Object:', objFromGlobal);
console.log('typeof objFromGlobal:', typeof objFromGlobal);
console.log('objFromGlobal === undefined:', objFromGlobal === undefined);
console.log('objFromGlobal === Object:', objFromGlobal === Object);

console.log('Direct Object:', Object);
console.log('global.Object === Object:', global.Object === Object);

const freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
console.log('freeGlobal:', freeGlobal);
console.log('typeof freeGlobal:', typeof freeGlobal);
