// Test whether accessing property on undefined throws
try {
    var x = undefined;
    var y = x.foo;
    console.log("No error!");
} catch (e) {
    console.log("Caught:", typeof e);
}
module.exports = {};
