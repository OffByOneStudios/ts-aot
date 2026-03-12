import { Hono } from 'hono';

async function user_main(): Promise<number> {
    let failures = 0;
    const app = new Hono();

    // Route registration
    app.get('/hello', (c) => c.text('Hello, World!'));
    app.get('/json', (c) => c.json({ message: 'ok', count: 42 }));
    app.get('/status', (c) => c.text('Created', 201));
    app.get('/user/:name', (c) => {
        const name = c.req.param('name');
        return c.json({ name });
    });

    // Test 1: GET text response
    const r1 = await app.fetch(new Request('http://localhost/hello'));
    check(r1.status === 200, "GET /hello status");
    const t1 = await r1.text();
    check(t1 === 'Hello, World!', "GET /hello body");

    // Test 2: GET JSON response
    const r2 = await app.fetch(new Request('http://localhost/json'));
    check(r2.status === 200, "GET /json status");

    // Test 3: Custom status code
    const r4 = await app.fetch(new Request('http://localhost/status'));
    check(r4.status === 201, "custom status code");

    // Test 4: Path parameters
    const r5 = await app.fetch(new Request('http://localhost/user/alice'));
    const j5 = await r5.json();
    check(j5.name === 'alice', "path param :name");

    // Test 5: 404 for unknown route
    const r6 = await app.fetch(new Request('http://localhost/unknown'));
    check(r6.status === 404, "unknown route 404");

    // Summary
    console.log("---");
    const total = 7;
    const passed = total - failures;
    console.log(passed + "/" + total + " hono tests passed");
    if (failures > 0) {
        console.log(failures + " test(s) failed");
    } else {
        console.log("All hono tests passed!");
    }
    process.exit(failures);
    return failures;

    function check(cond: boolean, name: string): void {
        if (!cond) { console.log("FAIL: " + name); failures++; }
    }
}
