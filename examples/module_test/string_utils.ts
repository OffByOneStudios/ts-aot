// String utilities module

export function greet(name: string): string {
    return "Hello, " + name + "!";
}

export function capitalize(str: string): string {
    if (str.length === 0) return str;
    return str[0].toUpperCase() + str.slice(1);
}

export function repeat(str: string, count: number): string {
    let result = "";
    for (let i = 0; i < count; i++) {
        result += str;
    }
    return result;
}
