// Simple undefined test without type annotation
var x = undefined;

if (x === undefined) {
    console.log("x is undefined (correct)");
} else {
    console.log("x is NOT undefined (BUG!)");
}
