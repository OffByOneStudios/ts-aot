interface HasLength {
    length: number;
}

function getLength<T extends HasLength>(arg: T): number {
    return arg.length;
}

function identity<T>(arg: T): T {
    return arg;
}

function first<T>(arr: T[]): T {
    return arr[0];
}

function main() {
    const s = "hello";
    const n = getLength(s); // Should infer T = string
    ts_console_log(s);
    ts_console_log(n);

    const x = identity(42); // Should infer T = number
    ts_console_log(x);

    const arr = [1, 2, 3];
    const y = first(arr); // Should infer T = number
    ts_console_log(y);
}
