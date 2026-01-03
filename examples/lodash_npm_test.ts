// Simple smoke test that exercises the installed lodash package.
// Uses CommonJS require to avoid needing type definitions.
// Expected output: chunked array, merged object, and shuffled length stays same.

// eslint-disable-next-line @typescript-eslint/no-var-requires
const _ = require("lodash");

function main() {
    const chunked = _.chunk([1, 2, 3, 4, 5], 2);
    console.log("chunk:", JSON.stringify(chunked));

    const merged = _.merge({ a: 1, nested: { x: 1 } }, { b: 2, nested: { y: 3 } });
    console.log("merge:", JSON.stringify(merged));

    const shuffled = _.shuffle([10, 20, 30, 40, 50]);
    console.log("shuffle length:", shuffled.length);
}

main();
