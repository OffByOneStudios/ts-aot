// Test os module and console.assert/debug
import * as os from 'os';

function user_main(): number {
    console.log("Testing os module...");

    // Test platform
    console.log("os.platform(): " + os.platform());

    // Test arch
    console.log("os.arch(): " + os.arch());

    // Test type
    console.log("os.type(): " + os.type());

    // Test hostname
    console.log("os.hostname(): " + os.hostname());

    // Test homedir
    console.log("os.homedir(): " + os.homedir());

    // Test tmpdir
    console.log("os.tmpdir(): " + os.tmpdir());

    // Test EOL
    console.log("os.EOL: " + JSON.stringify(os.EOL));

    // Test totalmem
    console.log("os.totalmem(): " + os.totalmem());

    // Test freemem
    console.log("os.freemem(): " + os.freemem());

    // Test uptime
    console.log("os.uptime(): " + os.uptime());

    // Test endianness
    console.log("os.endianness(): " + os.endianness());

    // Test loadavg
    const loadavg = os.loadavg();
    console.log("os.loadavg(): " + loadavg[0] + ", " + loadavg[1] + ", " + loadavg[2]);

    // Test cpus
    const cpus = os.cpus();
    console.log("os.cpus() count: " + cpus.length);
    if (cpus.length > 0) {
        console.log("  First CPU model: " + cpus[0].model);
        console.log("  First CPU speed: " + cpus[0].speed);
    }

    // Test userInfo
    const userInfo = os.userInfo();
    console.log("os.userInfo().username: " + userInfo.username);

    // Test console.debug
    console.log("\nTesting console.debug:");
    console.debug("This is a debug message");

    // Test console.assert
    console.log("\nTesting console.assert:");
    console.assert(true, "This should NOT print (true condition)");
    console.assert(1 === 1, "This should NOT print (1 === 1)");
    console.assert(false, "This SHOULD print (false condition)");
    console.assert(0 === 1, "This SHOULD print (0 === 1)");

    console.log("\nAll tests complete!");
    return 0;
}
