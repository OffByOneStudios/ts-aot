// Test readline module event functionality
import * as readline from 'readline';

function user_main(): number {
    console.log("=== readline events test ===");

    // Create interface with empty options
    const rl = readline.createInterface({});
    console.log("Interface created");

    // Test 1: Register line event listener
    console.log("Test 1: Registering line listener...");
    rl.on('line', (line: string) => {
        console.log("Line event received: " + line);
    });
    console.log("Line listener registered");

    // Test 2: Register close event listener
    console.log("Test 2: Registering close listener...");
    rl.on('close', () => {
        console.log("Close event received!");
    });
    console.log("Close listener registered");

    // Test 3: Register pause event listener
    console.log("Test 3: Registering pause listener...");
    rl.on('pause', () => {
        console.log("Pause event received!");
    });
    console.log("Pause listener registered");

    // Test 4: Register resume event listener
    console.log("Test 4: Registering resume listener...");
    rl.on('resume', () => {
        console.log("Resume event received!");
    });
    console.log("Resume listener registered");

    // Test pause/resume - should trigger events
    console.log("Test 5: Calling rl.pause()...");
    rl.pause();
    console.log("Test 6: Calling rl.resume()...");
    rl.resume();

    // Close the interface - should trigger 'close' event
    console.log("Test 7: Calling rl.close()...");
    rl.close();

    console.log("=== All events tests passed ===");
    return 0;
}
