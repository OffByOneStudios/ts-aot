const arr = [1, 2, 3, 4, 5];

// Test forEach
console.log("forEach:");
arr.forEach(x => console.log("  Item:", x));

// Test reduce - sum
const sum = arr.reduce((acc, x) => acc + x, 0);
console.log("Sum:", sum);

// Test some
const hasEven = arr.some(x => x % 2 === 0);
console.log("Has even:", hasEven);

// Test every
const allPositive = arr.every(x => x > 0);
console.log("All positive:", allPositive);
