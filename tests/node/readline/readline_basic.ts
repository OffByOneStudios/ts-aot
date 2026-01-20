// Test basic readline module functionality
import * as readline from 'readline';

function user_main(): number {
    console.log("=== readline module tests ===");

    // Test 1: Create interface with minimal options (empty object)
    console.log("Test 1: createInterface (empty options)");
    const rl = readline.createInterface({});
    console.log("Interface created successfully");

    // Test 2: Set and test prompt
    console.log("Test 2: setPrompt");
    rl.setPrompt(">>> ");
    console.log("Prompt set successfully");

    // Test 3: Test pause and resume (event-based, no actual interaction)
    console.log("Test 3: pause/resume");
    rl.pause();
    console.log("Paused");
    rl.resume();
    console.log("Resumed");

    // Test 4: Test close
    console.log("Test 4: close");
    console.log("About to call rl.close()");
    rl.close();
    console.log("Close called successfully");

    // Test 5: Test utility functions (these write ANSI sequences)
    console.log("Test 5: cursor utilities (ANSI)");
    readline.clearLine(process.stdout, 0);
    readline.cursorTo(process.stdout, 0);
    readline.moveCursor(process.stdout, 1, 0);
    console.log("Cursor utilities called");

    console.log("=== All readline tests passed ===");
    return 0;
}
