// Minimal test to debug dotenv - just import and check typeof
import * as dotenv from 'dotenv';

function user_main(): number {
    console.log("module loaded");
    console.log("typeof dotenv: " + typeof dotenv);
    return 0;
}
