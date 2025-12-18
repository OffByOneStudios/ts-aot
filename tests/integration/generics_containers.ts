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
    const b1 = new Box<number>(42);
    ts_console_log(b1.getValue());

    const b2 = new Box<string>("hello");
    ts_console_log(b2.getValue());

    const arr: number[] = [1, 2, 3];
    ts_console_log(arr.length);
}
