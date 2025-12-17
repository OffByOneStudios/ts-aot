function main() {
    let add = (a: number, b: number) => {
        return a + b;
    };
    console.log(add(10, 20));

    let square = (x: number) => x * x;
    console.log(square(5));

    let name = "World";
    let greeting = `Hello ${name}!`;
    console.log(greeting);
}
