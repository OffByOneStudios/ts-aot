// Simple test for util.inspect.custom
import * as util from 'util';

function user_main(): number {
    console.log("Testing util.inspect.custom");

    // Get the custom symbol
    const customSymbol = util.inspect.custom;
    console.log("customSymbol value: " + String(customSymbol));
    console.log("typeof customSymbol: " + typeof customSymbol);

    // Check if it's a symbol
    if (typeof customSymbol === "symbol") {
        console.log("PASS: customSymbol is a Symbol");
    } else {
        console.log("FAIL: customSymbol is not a Symbol");
    }

    // Test defaultOptions
    const opts = util.inspect.defaultOptions;
    console.log("typeof opts: " + typeof opts);
    console.log("opts.depth: " + opts.depth);
    console.log("opts.colors: " + opts.colors);

    return 0;
}
