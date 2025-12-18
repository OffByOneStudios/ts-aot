function sum(...numbers: number[]): number {
    let total = 0;
    for (const n of numbers) {
        total += n;
    }
    return total;
}

function main() {
    console.log(sum(1, 2, 3));
    console.log(sum(10, 20));
    console.log(sum());
}
