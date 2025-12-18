function main() {
    const add = function(a: number, b: number): number {
        return a + b;
    };

    const multiply = (a: number, b: number): number => {
        return a * b;
    };

    console.log(add(5, 3));
    console.log(multiply(4, 2));

    const ops = [add, multiply];
    for (const op of ops) {
        console.log(op(10, 20));
    }
}
