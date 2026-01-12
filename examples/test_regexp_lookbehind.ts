// Test RegExp lookbehind assertions - ES2018

function user_main(): number {
    console.log("Lookbehind test:");

    // Positive lookbehind: match digits preceded by $
    const price = /(?<=\$)\d+/;
    let match = price.exec("$100");
    if (match) {
        console.log(match[0]);  // "100"
    }

    // Should not match when not preceded by $
    match = price.exec("100");
    console.log(match === null);  // true

    // Negative lookbehind: match digits NOT preceded by $
    const notPrice = /(?<!\$)\d+/;
    match = notPrice.exec("100");
    if (match) {
        console.log(match[0]);  // "100"
    }

    return 0;
}
