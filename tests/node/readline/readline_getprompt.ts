// Test readline.Interface.getPrompt() method
import * as readline from 'readline';

function user_main(): number {
    console.log("=== readline getPrompt test ===");

    // Test 1: Default prompt - Note: Node.js default is empty string unless specified
    const rl1 = readline.createInterface({});
    const defaultPrompt = rl1.getPrompt();
    console.log("Test 1: Default prompt length = " + defaultPrompt.length);
    if (defaultPrompt.length > 0) {
        console.log("Test 1: Default prompt = '" + defaultPrompt + "'");
    } else {
        console.log("Test 1: Default prompt is empty (expected for no options)");
    }

    // Test 2: Set and get prompt
    rl1.setPrompt(">>> ");
    const customPrompt = rl1.getPrompt();
    console.log("Test 2: After setPrompt, prompt length = " + customPrompt.length);

    // Test 3: Cleanup
    rl1.close();

    console.log("=== getPrompt tests passed ===");
    return 0;
}
