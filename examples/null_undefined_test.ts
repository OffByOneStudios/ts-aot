const x = undefined;
const y = null;

if (x === undefined) {
    console.log('x is undefined');
}

if (y === null) {
    console.log('y is null');
}

function test() {
    // No return - should return undefined
}

const result = test();
if (result === undefined) {
    console.log('test() returns undefined');
}

console.log('done');
