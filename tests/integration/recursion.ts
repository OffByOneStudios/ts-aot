function factorial(n: number): number {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

function main() {
    console.log("Factorial of 5:");
    console.log(factorial(5));
}
