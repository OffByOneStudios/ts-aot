// Test negate function

function negate(fn: (x: number) => boolean): (x: number) => boolean {
    return (x: number): boolean => {
        let res = fn(x);
        return !res;
    };
}

function isEven(n: number): boolean {
    return n % 2 === 0;
}

const isOdd = negate(isEven);

console.log("Test negate:");
console.log(isOdd(1));  // Should print true
console.log(isOdd(2));  // Should print false
console.log(isOdd(3));  // Should print true
console.log(isOdd(4));  // Should print false
