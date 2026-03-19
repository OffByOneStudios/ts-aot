const express = require('../tests/npm/express/node_modules/express');
function user_main(): number {
    console.log("Creating app...");
    const app = express();
    console.log("app created, app.listen:", typeof app.listen);
    try {
        console.log("Calling listen...");
        const server = app.listen(3456, function() {
            console.log('Express server running on port 3456');
        });
        console.log("listen returned:", typeof server);
    } catch (e: any) {
        console.log("ERROR:", e.message);
    }
    return 0;
}
