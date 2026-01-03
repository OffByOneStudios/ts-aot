function sumDefaults(a: number = 10, b: number = 5): number {
    return a + b;
}

function greet(name: string = "Anon", salutation: string = "Hi"): string {
    return `${salutation}, ${name}!`;
}

function main() {
    // Passing undefined should trigger the default value at runtime
    console.log("sumDefaults(undefined, 2) =", sumDefaults(undefined, 2)); // 12
    console.log("sumDefaults(3, undefined) =", sumDefaults(3, undefined)); // 8
    console.log("sumDefaults(undefined, undefined) =", sumDefaults(undefined, undefined)); // 15

    console.log("greet() =", greet()); // Hi, Anon!
    console.log('greet(undefined, "Yo") =', greet(undefined, "Yo")); // Yo, Anon!
    console.log('greet("Sam", undefined) =', greet("Sam", undefined)); // Hi, Sam!
}

main();
