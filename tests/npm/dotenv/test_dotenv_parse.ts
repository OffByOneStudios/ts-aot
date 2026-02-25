function user_main(): number {
    // Test 1: simple exec with mg flags
    console.log("test 1: simple /mg exec");
    const re1 = /(\w+)=(\w+)/mg;
    const s1 = 'FOO=bar';
    const m1 = re1.exec(s1);
    console.log("m1: " + (m1 ? m1[0] : "null"));

    // Test 2: The exact dotenv LINE regex
    console.log("test 2: dotenv LINE regex");
    const LINE = /(?:^|^)\s*(?:export\s+)?([\w.-]+)(?:\s*=\s*?|:\s+?)(\s*'(?:\\'|[^'])*'|\s*"(?:\\"|[^"])*"|\s*`(?:\\`|[^`])*`|[^#\r\n]+)?\s*(?:#.*)?(?:$|$)/mg;
    const s2 = 'FOO=bar';
    console.log("about to exec");
    const m2 = LINE.exec(s2);
    console.log("exec done: " + (m2 ? "match" : "null"));
    if (m2) {
        console.log("m2[0]: " + m2[0]);
        console.log("m2[1]: " + m2[1]);
    }
    console.log("done");
    return 0;
}
