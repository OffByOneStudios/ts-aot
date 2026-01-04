const arr: number[] = [1, 2, 3];
console.log("About to call map");
const result = arr.map((x: number) => {
    console.log("In callback");
    return x * 2;
});
console.log("Result:", result);
