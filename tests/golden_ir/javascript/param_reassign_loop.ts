// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT:     x
// OUTPUT: hello

function padLeft(s: string, width: number): string {
    while (s.length < width) { s = ' ' + s; }
    return s;
}

function repeat(s: string, n: number): string {
    let result = '';
    while (n > 0) {
        result = result + s;
        n = n - 1;
    }
    return result;
}

function user_main(): number {
    console.log(padLeft('x', 5));
    console.log(repeat('hello', 1));
    return 0;
}
