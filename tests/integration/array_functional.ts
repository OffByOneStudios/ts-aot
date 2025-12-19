function main() {
    let arr = [1, 2, 3, 4, 5];
    
    console.log("Original array:");
    for (let i = 0; i < arr.length; i = i + 1) {
        console.log(arr[i]);
    }

    console.log("Map (v * 2):");
    let doubled = arr.map(v => v * 2);
    for (let i = 0; i < doubled.length; i = i + 1) {
        console.log(doubled[i]);
    }

    console.log("Filter (v > 2):");
    let filtered = arr.filter(v => v > 2);
    for (let i = 0; i < filtered.length; i = i + 1) {
        console.log(filtered[i]);
    }

    console.log("Reduce (sum):");
    let sum = arr.reduce((acc, v) => acc + v, 0);
    console.log(sum);

    console.log("Find (v > 3):");
    let found = arr.find(v => v > 3);
    console.log(found);

    console.log("FindIndex (v > 3):");
    let foundIndex = arr.findIndex(v => v > 3);
    console.log(foundIndex);

    console.log("Some (v > 4):");
    console.log(arr.some(v => v > 4));

    console.log("Every (v > 0):");
    console.log(arr.every(v => v > 0));

    console.log("ForEach:");
    arr.forEach(v => console.log(v));
}
