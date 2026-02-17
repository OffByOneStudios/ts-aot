// Library file: exported Counter class for cross-module testing

export class Counter {
    private count: number;

    constructor(initial: number) {
        this.count = initial;
    }

    increment(): void {
        this.count++;
    }

    decrement(): void {
        this.count--;
    }

    getCount(): number {
        return this.count;
    }

    static create(): Counter {
        return new Counter(0);
    }
}
