interface HasLength {
    length: number;
}

function getLength<T extends HasLength>(arg: T): number {
    return arg.length;
}

function user_main(): number {
    let s = "hello";
    console.log("" + getLength<string>(s));
    // getLength<number>(42) would be a compile error (number has no length)
    console.log("" + getLength("world")); // Infers T=string
    let arr: number[] = [1, 2, 3];
    console.log("" + getLength(arr)); // Infers T=number[]
    return 0;
}
