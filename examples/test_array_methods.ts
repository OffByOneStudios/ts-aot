const arr = [1, 2, 3, 4, 5];

// Test map
const doubled = arr.map(x => x * 2);
console.log("Doubled:", doubled);

// Test filter  
const evens = arr.filter(x => x % 2 === 0);
console.log("Evens:", evens);

// Test chaining
const result = arr.filter(x => x > 2).map(x => x * 10);
console.log("Chained:", result);
