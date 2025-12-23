async function* asyncGen() {
    yield 1;
    yield 2;
    yield 3;
}

async function test() {
    let sum = 0;
    for await (const x of asyncGen()) {
        sum += x;
    }
    return sum;
}

// @ts-ignore
export function main() {
    return test();
}
