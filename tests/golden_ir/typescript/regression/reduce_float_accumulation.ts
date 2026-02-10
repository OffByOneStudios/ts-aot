// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: 1.50
// OUTPUT: 3.00

function user_main(): number {
    const values: number[] = [0.5, 0.5, 0.5];
    const sum = values.reduce((a: number, b: number) => a + b, 0);
    console.log(sum.toFixed(2));

    const doubled = sum * 2;
    console.log(doubled.toFixed(2));

    return 0;
}
