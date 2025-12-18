function identity<T>(arg: T): T {
    return arg;
}

class Box<T> {
    value: T;
    constructor(v: T) {
        this.value = v;
    }
    getValue(): T {
        return this.value;
    }
}

function main() {
    const x = identity<number>(10);
    ts_console_log(x);

    const b = new Box<number>(42);
    ts_console_log(b.getValue());
}

main();
