// Test global.Object
console.log("1. global:", global);
console.log("2. Object:", Object);
console.log("3. typeof global.Object:", typeof (global as any).Object);
console.log("4. global.Object === Object:", (global as any).Object === Object);
