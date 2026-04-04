import picomatch from 'picomatch';

function user_main(): number {
    const isJs = picomatch("*.js");
    const result = isJs("test.js");
    console.log("C: " + result);
    return 0;
}
