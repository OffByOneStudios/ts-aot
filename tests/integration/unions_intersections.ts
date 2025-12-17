interface Circle { kind: string, radius: number }
interface Square { kind: string, side: number }

function main() {
    let shape: Circle | Square = { kind: "circle", radius: 10 };
    ts_console_log(shape.kind);
    
    shape = { kind: "square", side: 5 };
    ts_console_log(shape.kind);

    // Intersection test
    interface A { a: number }
    interface B { b: number }
    let y: A & B = { a: 1, b: 2 };
    ts_console_log(y.a);
    ts_console_log(y.b);
}
