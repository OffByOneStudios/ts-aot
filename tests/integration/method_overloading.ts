class Overloader {
    static count: number = 0;

    constructor(x: number);
    constructor(x: string);
    constructor(x: any) {
        Overloader.count = Overloader.count + 1;
    }

    foo(x: number): number;
    foo(x: string): string;
    foo(x: any): any {
        Overloader.count = Overloader.count + 1;
        return x;
    }

    static bar(x: number): void;
    static bar(x: string): void;
    static bar(x: any): void {
        Overloader.count = Overloader.count + 1;
    }
}

function main() {
    const o1 = new Overloader(10);
    const o2 = new Overloader("hello");

    o1.foo(20);
    o1.foo("world");

    Overloader.bar(30);
    Overloader.bar("static");

    console.log(Overloader.count);
}
