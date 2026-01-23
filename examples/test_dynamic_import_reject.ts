// Test dynamic import rejection for non-static modules

async function user_main(): Promise<number> {
    console.log("Starting test...");

    try {
        // Dynamic import with non-existent module - should reject
        const mod = await import('./nonexistent-module');
        console.log("ERROR: Should have rejected!");
    } catch (err: any) {
        console.log("Caught expected error:");
        console.log(err.message || String(err));
    }

    console.log("Test complete");
    return 0;
}
