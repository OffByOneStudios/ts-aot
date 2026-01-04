function foo() { return 1; }
console.log("foo.length:", foo.length);
console.log("type:", typeof foo.length);
if (foo.length === 0) {
    console.log("length is 0");
} else {
    console.log("length is NOT 0");
}
console.log("done");
