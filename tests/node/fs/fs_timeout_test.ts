// Test setTimeout basic
async function user_main(): Promise<number> {
    console.log("=== setTimeout Test ===");

    let fired = false;

    setTimeout(function() {
        console.log("Timeout fired!");
        fired = true;
    }, 100);

    console.log("Timeout scheduled");
    console.log("fired before await: " + fired);

    // Wait for the timeout
    await new Promise<void>((resolve) => {
        setTimeout(function() {
            resolve();
        }, 200);
    });

    console.log("fired after await: " + fired);

    return fired ? 0 : 1;
}
