// Test basic async_hooks module functionality

import * as async_hooks from 'async_hooks';

function user_main(): number {
    console.log("=== Testing async_hooks module ===");

    // Test 1: executionAsyncId
    console.log("Test 1: executionAsyncId()");
    const asyncId = async_hooks.executionAsyncId();
    console.log("executionAsyncId:", asyncId);
    if (asyncId >= 1) {
        console.log("PASS: executionAsyncId returns valid ID");
    } else {
        console.log("FAIL: executionAsyncId should return >= 1");
        return 1;
    }

    // Test 2: triggerAsyncId
    console.log("Test 2: triggerAsyncId()");
    const triggerId = async_hooks.triggerAsyncId();
    console.log("triggerAsyncId:", triggerId);
    if (triggerId >= 0) {
        console.log("PASS: triggerAsyncId returns valid ID");
    } else {
        console.log("FAIL: triggerAsyncId should return >= 0");
        return 1;
    }

    // Test 3: AsyncLocalStorage basic
    console.log("Test 3: AsyncLocalStorage basic");
    const als = new async_hooks.AsyncLocalStorage();
    console.log("Created AsyncLocalStorage");

    // getStore should return undefined before run
    const storeBefore = als.getStore();
    console.log("Store before run:", storeBefore);

    // Test 4: AsyncLocalStorage.run
    console.log("Test 4: AsyncLocalStorage.run()");
    let resultFromRun: number = 0;
    als.run({ value: 42 }, () => {
        const store = als.getStore();
        console.log("Store inside run:", store);
        if (store && store.value === 42) {
            console.log("PASS: Store has correct value inside run");
            resultFromRun = 1;
        } else {
            console.log("FAIL: Store value incorrect inside run");
        }
    });

    if (resultFromRun === 1) {
        console.log("PASS: run() executed callback");
    } else {
        console.log("FAIL: run() callback didn't execute properly");
        return 1;
    }

    // Test 5: Store should be undefined after run
    const storeAfter = als.getStore();
    console.log("Store after run:", storeAfter);

    // Test 6: AsyncLocalStorage.enterWith
    console.log("Test 6: AsyncLocalStorage.enterWith()");
    als.enterWith({ value: 100 });
    const storeAfterEnter = als.getStore();
    if (storeAfterEnter && storeAfterEnter.value === 100) {
        console.log("PASS: enterWith set the store value");
    } else {
        console.log("FAIL: enterWith didn't set store correctly");
        return 1;
    }

    // Test 7: AsyncLocalStorage.disable
    console.log("Test 7: AsyncLocalStorage.disable()");
    als.disable();
    const storeAfterDisable = als.getStore();
    console.log("Store after disable:", storeAfterDisable);

    // Test 8: AsyncResource
    console.log("Test 8: AsyncResource");
    const resource = new async_hooks.AsyncResource("TestResource");
    const resourceId = resource.asyncId();
    const resourceTriggerId = resource.triggerAsyncId();
    console.log("AsyncResource asyncId:", resourceId);
    console.log("AsyncResource triggerAsyncId:", resourceTriggerId);

    if (resourceId > 0) {
        console.log("PASS: AsyncResource has valid asyncId");
    } else {
        console.log("FAIL: AsyncResource asyncId should be > 0");
        return 1;
    }

    // Test 9: AsyncResource.runInAsyncScope
    console.log("Test 9: AsyncResource.runInAsyncScope()");
    let scopeExecuted = false;
    resource.runInAsyncScope(() => {
        scopeExecuted = true;
        console.log("Inside runInAsyncScope");
    }, null);

    if (scopeExecuted) {
        console.log("PASS: runInAsyncScope executed callback");
    } else {
        console.log("FAIL: runInAsyncScope didn't execute callback");
        return 1;
    }

    // Test 10: createHook
    console.log("Test 10: createHook()");
    let initCalled = false;
    const hook = async_hooks.createHook({
        init: (asyncId: number, type: string, triggerAsyncId: number) => {
            initCalled = true;
            console.log("Hook init called:", asyncId, type, triggerAsyncId);
        }
    });
    hook.enable();
    console.log("Hook enabled");
    hook.disable();
    console.log("Hook disabled");

    console.log("\n=== All async_hooks tests passed! ===");
    return 0;
}
