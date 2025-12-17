function main() {
    let arr = [10, 20, 30];
    console.log(arr[0]);
    console.log(arr[1]);
    console.log(arr[2]);
    
    arr[1] = 42;
    console.log(arr[1]);
    
    let sum = 0;
    for (let i = 0; i < 3; i = i + 1) {
        sum = sum + arr[i];
    }
    console.log(sum);
}
