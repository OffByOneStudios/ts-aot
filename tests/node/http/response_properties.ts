// Test for HTTP module constants
import * as http from 'http';

function user_main(): number {
    console.log("Testing HTTP module constants...");

    // Test http.maxHeaderSize
    const maxHeaderSize = http.maxHeaderSize;
    console.log("maxHeaderSize = " + maxHeaderSize);
    if (maxHeaderSize === 16384) {
        console.log("PASS: http.maxHeaderSize is 16384");
    } else {
        console.log("FAIL: http.maxHeaderSize is not 16384, got " + maxHeaderSize);
        return 1;
    }

    console.log("Results: 1 passed, 0 failed");
    return 0;
}
