interface HasLength {
    length: number;
}

function getLength<T extends HasLength>(arg: T): number {
    return arg.length;
}

function main() {
    let s = "hello";
    ts_console_log(getLength<string>(s));
    ts_console_log(getLength<number>(42)); // Should fail

    ts_console_log(getLength("world")); // Should infer T=string
}
