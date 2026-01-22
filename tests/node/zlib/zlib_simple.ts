// Simple zlib test to debug
import * as zlib from 'zlib';

function user_main(): number {
    console.log("Step 1: Getting constants");
    const constants = zlib.constants;
    console.log("Step 2: Got constants");

    if (constants) {
        console.log("Constants is truthy");
    } else {
        console.log("Constants is falsy");
        return 1;
    }

    console.log("Step 3: Checking Z_NO_FLUSH");
    const val = constants.Z_NO_FLUSH;
    console.log("Z_NO_FLUSH = " + val);

    return 0;
}
