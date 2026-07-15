// readFileSync encoding argument: string encoding or { encoding } options
// must return a STRING (node semantics); no encoding returns a Buffer.
// Regression: the encoding argument was dropped entirely — a Buffer came
// back and `readFileSync(f, "utf8") === "..."` was always false.
import * as fs from 'fs';

function user_main(): number {
    let failures = 0;

    fs.writeFileSync("tmp/fs_readfile_encoding.txt", "hello world");

    const s = fs.readFileSync("tmp/fs_readfile_encoding.txt", "utf8");
    if (typeof s === "string" && s === "hello world") {
        console.log("PASS: string encoding arg returns matching string");
    } else {
        console.log("FAIL: utf8 arg gave typeof=" + typeof s + " value=" + s);
        failures++;
    }

    const s2 = fs.readFileSync("tmp/fs_readfile_encoding.txt", { encoding: "utf8" });
    if (typeof s2 === "string" && s2 === "hello world") {
        console.log("PASS: options-object encoding returns matching string");
    } else {
        console.log("FAIL: options object gave typeof=" + typeof s2);
        failures++;
    }

    const b = fs.readFileSync("tmp/fs_readfile_encoding.txt");
    if (typeof b === "object" && b.length === 11 && b[0] === 104) {
        console.log("PASS: no encoding returns Buffer");
    } else {
        console.log("FAIL: no-encoding form typeof=" + typeof b + " len=" + b.length);
        failures++;
    }

    console.log(failures === 0 ? "All readFileSync encoding tests passed!"
                               : failures + " test(s) failed");
    return failures;
}
