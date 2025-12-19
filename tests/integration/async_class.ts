class AsyncTester {
    private value: number;

    constructor(v: number) {
        this.value = v;
    }

    async getValue(): Promise<number> {
        return this.value;
    }

    async add(x: number): Promise<number> {
        const current = await this.getValue();
        return current + x;
    }
}

async function main() {
    const tester = new AsyncTester(10);
    const result = await tester.add(5);
    console.log("Result:", result);
}

main();
