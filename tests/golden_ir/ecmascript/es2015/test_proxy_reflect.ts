// Test Proxy and Reflect ES2015 features

function user_main(): number {
    console.log("Testing Proxy and Reflect...");

    // Test 1: Basic Proxy with get trap
    console.log("\n1. Basic Proxy with get trap:");
    const target1 = { message: "hello", value: 42 };
    const handler1 = {
        get: function(obj: any, prop: any, receiver: any): any {
            console.log("get trap called for: " + prop);
            return Reflect.get(obj, prop, receiver);
        }
    };
    const proxy1 = new Proxy(target1, handler1);
    console.log("proxy1.message = " + proxy1.message);
    console.log("proxy1.value = " + proxy1.value);

    // Test 2: Proxy with set trap
    console.log("\n2. Proxy with set trap:");
    const target2: any = { count: 0 };
    const handler2 = {
        set: function(obj: any, prop: any, value: any, receiver: any): boolean {
            console.log("set trap: " + prop + " = " + value);
            return Reflect.set(obj, prop, value, receiver);
        }
    };
    const proxy2 = new Proxy(target2, handler2);
    proxy2.count = 5;
    console.log("target2.count = " + target2.count);

    // Test 3: Reflect.get and Reflect.set
    console.log("\n3. Reflect.get and Reflect.set:");
    const obj3 = { x: 10, y: 20 };
    console.log("Reflect.get(obj3, 'x') = " + Reflect.get(obj3, "x"));
    Reflect.set(obj3, "x", 100);
    console.log("After Reflect.set: obj3.x = " + obj3.x);

    // Test 4: Reflect.has
    console.log("\n4. Reflect.has:");
    const obj4 = { name: "test" };
    console.log("Reflect.has(obj4, 'name') = " + Reflect.has(obj4, "name"));
    console.log("Reflect.has(obj4, 'missing') = " + Reflect.has(obj4, "missing"));

    // Test 5: Reflect.deleteProperty
    console.log("\n5. Reflect.deleteProperty:");
    const obj5: any = { a: 1, b: 2 };
    console.log("Before delete: " + Object.keys(obj5).join(", "));
    Reflect.deleteProperty(obj5, "a");
    console.log("After delete 'a': " + Object.keys(obj5).join(", "));

    // Test 6: Reflect.ownKeys
    console.log("\n6. Reflect.ownKeys:");
    const obj6 = { foo: 1, bar: 2, baz: 3 };
    const keys = Reflect.ownKeys(obj6);
    console.log("Reflect.ownKeys: " + keys.join(", "));

    // Test 7: Proxy with has trap
    console.log("\n7. Proxy with has trap:");
    const target7 = { secret: "hidden", visible: "shown" };
    const handler7 = {
        has: function(obj: any, prop: any): boolean {
            if (prop === "secret") {
                console.log("has trap: hiding 'secret'");
                return false;
            }
            return Reflect.has(obj, prop);
        }
    };
    const proxy7 = new Proxy(target7, handler7);
    console.log("'visible' in proxy7: " + ("visible" in proxy7));
    console.log("'secret' in proxy7: " + ("secret" in proxy7));

    // Test 8: Proxy.revocable
    console.log("\n8. Proxy.revocable:");
    const target8 = { data: "important" };
    const { proxy: proxy8, revoke } = Proxy.revocable(target8, {});
    console.log("Before revoke: proxy8.data = " + proxy8.data);
    revoke();
    console.log("After revoke: proxy operations will fail");

    console.log("\nAll Proxy and Reflect tests passed!");
    return 0;
}
