function greet(name: string, greeting: string = "Hello"): string {
    return greeting + ", " + name + "!";
}

function add(a: number, b?: number): number {
    if (b === undefined) {
        return a;
    }
    return a + b;
}

function main() {
    console.log(greet("World"));
    console.log(greet("Copilot", "Hi"));
    
    console.log(add(10));
    console.log(add(10, 20));
}
