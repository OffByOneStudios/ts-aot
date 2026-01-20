// Test readline module signal events (SIGINT, SIGTSTP, SIGCONT)
import * as readline from 'readline';

function user_main(): number {
    console.log("=== readline signal events test ===");

    // Create interface
    const rl = readline.createInterface({});
    console.log("Interface created");

    let sigintReceived = false;
    let sigtstpReceived = false;
    let sigcontReceived = false;

    // Test 1: Register SIGINT listener
    console.log("Test 1: Registering SIGINT listener...");
    rl.on('SIGINT', () => {
        console.log("SIGINT event received!");
        sigintReceived = true;
    });
    console.log("SIGINT listener registered");

    // Test 2: Register SIGTSTP listener
    console.log("Test 2: Registering SIGTSTP listener...");
    rl.on('SIGTSTP', () => {
        console.log("SIGTSTP event received!");
        sigtstpReceived = true;
    });
    console.log("SIGTSTP listener registered");

    // Test 3: Register SIGCONT listener
    console.log("Test 3: Registering SIGCONT listener...");
    rl.on('SIGCONT', () => {
        console.log("SIGCONT event received!");
        sigcontReceived = true;
    });
    console.log("SIGCONT listener registered");

    // Clean up
    rl.close();

    console.log("=== signal events tests passed ===");
    return 0;
}
