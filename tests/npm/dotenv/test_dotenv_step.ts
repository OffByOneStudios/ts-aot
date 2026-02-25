import * as dotenv from 'dotenv';

function user_main(): number {
    console.log("step 1: module loaded");
    console.log("typeof dotenv.parse: " + typeof dotenv.parse);

    console.log("step 2: calling parse");
    const result = dotenv.parse('FOO=bar\nBAZ=qux');
    console.log("step 3: parse returned");
    console.log("typeof result: " + typeof result);

    if (result) {
        const keys = Object.keys(result);
        console.log("result keys: " + keys.length);
        for (let i = 0; i < keys.length; i++) {
            console.log("  " + keys[i] + " = " + (result as any)[keys[i]]);
        }
    }

    return 0;
}
