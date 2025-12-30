// Debug Set.has with array elements
const arr1 = [2, 3, 4];
const arr2 = [1, 2, 3];

// Create set from arr1
const s = new Set<number>();
let i = 0;
while (i < arr1.length) {
    console.log("Adding:");
    console.log(arr1[i]);
    s.add(arr1[i]);
    i = i + 1;
}

console.log("Set size:");
console.log(s.size);

// Check if values from arr2 are in set
console.log("Has 1?");
console.log(s.has(1));

console.log("Has 2?");
console.log(s.has(2));

console.log("Has 3?");
console.log(s.has(3));

// Check with literal values
console.log("Has literal 2?");
const two = 2;
console.log(s.has(two));

// Check what was added
console.log("Checking arr1[0]:");
console.log(arr1[0]);
console.log("Has arr1[0]?");
console.log(s.has(arr1[0]));
