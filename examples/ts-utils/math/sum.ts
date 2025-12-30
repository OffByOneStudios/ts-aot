// Sum all numbers in array
export function sum(array: number[]): number {
    let total = 0;
    for (const n of array) {
        total = total + n;
    }
    return total;
}

// Calculate average of numbers
export function avg(array: number[]): number {
    if (array.length === 0) return 0;
    return sum(array) / array.length;
}

// Find min value
export function min(array: number[]): number {
    let result = array[0];
    for (const n of array) {
        if (n < result) result = n;
    }
    return result;
}

// Find max value
export function max(array: number[]): number {
    let result = array[0];
    for (const n of array) {
        if (n > result) result = n;
    }
    return result;
}
