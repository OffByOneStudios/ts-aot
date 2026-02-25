function myFunc(a, b) {
    return a + b;
}

function user_main() {
    // Test typeof on a function reference
    console.log("typeof myFunc: " + typeof myFunc);

    // Test calling it
    var r = myFunc(1, 2);
    console.log("result: " + r);

    // Test object with function property
    var obj = { fn: myFunc, name: "test" };
    console.log("typeof obj.fn: " + typeof obj.fn);
    console.log("typeof obj.name: " + typeof obj.name);

    // Test module.exports pattern
    var exports = { myFunc: myFunc };
    console.log("typeof exports.myFunc: " + typeof exports.myFunc);

    return 0;
}
