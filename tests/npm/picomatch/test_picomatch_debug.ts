import picomatch from 'picomatch';

function user_main(): number {
    console.log("1: picomatch imported");
    console.log("2: typeof picomatch = " + typeof picomatch);

    const isJs = picomatch("*.js");
    console.log("3: isJs created, typeof = " + typeof isJs);

    const result = isJs("test.js");
    console.log("4: result = " + result);

    return 0;
}
