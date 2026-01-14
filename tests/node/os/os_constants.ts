// Test os.constants
import * as os from 'os';

function user_main(): number {
    console.log("Testing os.constants...");

    // Test os.constants exists
    const constants = os.constants;
    console.log("os.constants exists: " + (constants !== undefined));

    // Test os.constants.signals
    const signals = os.constants.signals;
    console.log("os.constants.signals exists: " + (signals !== undefined));

    // Test specific signal values
    console.log("SIGINT: " + os.constants.signals.SIGINT);
    console.log("SIGTERM: " + os.constants.signals.SIGTERM);
    console.log("SIGKILL: " + os.constants.signals.SIGKILL);

    // Test os.constants.errno
    const errno = os.constants.errno;
    console.log("os.constants.errno exists: " + (errno !== undefined));

    // Test specific errno values
    console.log("ENOENT: " + os.constants.errno.ENOENT);
    console.log("EACCES: " + os.constants.errno.EACCES);
    console.log("EEXIST: " + os.constants.errno.EEXIST);

    // Test os.constants.priority
    const priority = os.constants.priority;
    console.log("os.constants.priority exists: " + (priority !== undefined));

    // Test specific priority values
    console.log("PRIORITY_LOW: " + os.constants.priority.PRIORITY_LOW);
    console.log("PRIORITY_NORMAL: " + os.constants.priority.PRIORITY_NORMAL);
    console.log("PRIORITY_HIGH: " + os.constants.priority.PRIORITY_HIGH);
    console.log("PRIORITY_HIGHEST: " + os.constants.priority.PRIORITY_HIGHEST);

    console.log("All os.constants tests passed!");
    return 0;
}
