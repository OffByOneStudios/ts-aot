function identity<T>(arg: T): T {
    return arg;
}

function main() {
    let x = identity<number>(42);
    ts_console_log(x);
    
    let y = identity<string>("hello");
    ts_console_log(y);
}
