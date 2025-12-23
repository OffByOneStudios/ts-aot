function sumFloat64(arr: Float64Array): number {
    let sum = 0;
    for (let i = 0; i < arr.length; i++) {
        sum += arr[i];
    }
    return sum;
}

sumFloat64(new Float64Array([1, 2, 3]));
