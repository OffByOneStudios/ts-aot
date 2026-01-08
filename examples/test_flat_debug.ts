function user_main(): number {
    const nested: number[][] = [[1, 2], [3, 4]];
    console.log("Outer length:");
    console.log(nested.length);

    const first = nested[0];
    console.log("First inner length:");
    console.log(first.length);

    const flat1 = nested.flat();
    console.log("Flat length:");
    console.log(flat1.length);

    return 0;
}
