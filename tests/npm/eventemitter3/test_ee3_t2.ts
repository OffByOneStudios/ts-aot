import EventEmitter from 'eventemitter3';

function user_main(): number {
    // Test 2: emit with argument
    console.log("creating ee2");
    const ee2: any = new EventEmitter();
    let received: string = "";
    console.log("adding listener");
    ee2.on("data", (msg: string) => {
        console.log("listener called with: " + String(msg));
        received = msg;
    });
    console.log("emitting data");
    ee2.emit("data", "hello");
    console.log("received: " + String(received));
    console.log("comparing...");
    if (received === "hello") {
        console.log("PASS");
    } else {
        console.log("FAIL: got '" + received + "'");
    }
    return 0;
}
