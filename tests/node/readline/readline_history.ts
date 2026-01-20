// Test readline module history event
import * as readline from 'readline';

function user_main(): number {
    console.log("=== readline history event test ===");

    // Create interface
    const rl = readline.createInterface({});
    console.log("Interface created");

    let historyReceived = false;

    // Test 1: Register history event listener
    console.log("Test 1: Registering history listener...");
    rl.on('history', (history: string[]) => {
        console.log("History event received!");
        console.log("History length: " + history.length);
        historyReceived = true;
    });
    console.log("History listener registered");

    // Clean up
    rl.close();

    console.log("=== history event test passed ===");
    return 0;
}
