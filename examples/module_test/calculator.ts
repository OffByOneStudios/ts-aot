// Calculator class with default export

export default class Calculator {
    private result: number;

    constructor(initial: number = 0) {
        this.result = initial;
    }

    add(value: number): number {
        this.result += value;
        return this.result;
    }

    subtract(value: number): number {
        this.result -= value;
        return this.result;
    }

    multiply(value: number): number {
        this.result *= value;
        return this.result;
    }

    reset(): void {
        this.result = 0;
    }

    getResult(): number {
        return this.result;
    }
}
