// Test prefix increment

let index = -1;
console.log("Before: " + index);

++index;
console.log("After ++index: " + index);

++index;
console.log("After ++index again: " + index);

// Test in while condition
let i = 0;
while (++i < 3) {
    console.log("In loop: i=" + i);
}
console.log("After loop: i=" + i);
