// Test if global.Object === Object
console.log("typeof global:", typeof global);
console.log("typeof global.Object:", typeof global.Object);
console.log("typeof Object:", typeof Object);
console.log("global.Object === Object:", global.Object === Object);

// Test the freeGlobal condition
const freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
console.log("freeGlobal:", freeGlobal);
console.log("freeGlobal is truthy:", !!freeGlobal);
