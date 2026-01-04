// Simple undefined test
var x: any = undefined;

if (x === undefined) {
    console.log("x is undefined (correct)");
} else {
    console.log("x is NOT undefined (BUG!)");
}
