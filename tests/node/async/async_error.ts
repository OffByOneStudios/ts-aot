// Async/Await Error Handling Tests

async function throwErrorAsync(): Promise<number> {
  throw new Error('Async error');
}

async function rejectPromise(): Promise<number> {
  return Promise.reject('Rejected promise');
}

async function user_main(): Promise<number> {
  let failures = 0;
  console.log('=== Async/Await Error Handling Tests ===\n');

  // Test 1: Try-catch with async error
  try {
    let caught = false;
    try {
      await throwErrorAsync();
    } catch (e) {
      caught = true;
    }
    if (!caught) {
      console.log('FAIL: Async error not caught');
      failures++;
    } else {
      console.log('PASS: Try-catch with async error');
    }
  } catch (e) {
    console.log('FAIL: Try-catch async - Exception');
    failures++;
  }

  // Test 2: Try-catch with Promise.reject
  try {
    let caught = false;
    try {
      await Promise.reject('test error');
    } catch (e) {
      caught = true;
    }
    if (!caught) {
      console.log('FAIL: Promise.reject not caught');
      failures++;
    } else {
      console.log('PASS: Try-catch with Promise.reject');
    }
  } catch (e) {
    console.log('FAIL: Try-catch reject - Exception');
    failures++;
  }

  // Test 3: Finally block execution
  try {
    let finallyExecuted = false;
    try {
      await Promise.reject('error');
    } catch (e) {
      // Caught
    } finally {
      finallyExecuted = true;
    }
    if (!finallyExecuted) {
      console.log('FAIL: Finally block not executed');
      failures++;
    } else {
      console.log('PASS: Finally block execution');
    }
  } catch (e) {
    console.log('FAIL: Finally block - Exception');
    failures++;
  }

  // Test 4: Error propagation through async calls
  try {
    async function caller(): Promise<number> {
      return await throwErrorAsync();
    }
    let caught = false;
    try {
      await caller();
    } catch (e) {
      caught = true;
    }
    if (!caught) {
      console.log('FAIL: Error not propagated through async calls');
      failures++;
    } else {
      console.log('PASS: Error propagation');
    }
  } catch (e) {
    console.log('FAIL: Error propagation - Exception');
    failures++;
  }

  // Test 5: Multiple try-catch blocks
  try {
    let catches = 0;
    try {
      await Promise.reject('error 1');
    } catch (e) {
      catches++;
    }
    try {
      await Promise.reject('error 2');
    } catch (e) {
      catches++;
    }
    if (catches !== 2) {
      console.log('FAIL: Multiple try-catch blocks wrong count');
      failures++;
    } else {
      console.log('PASS: Multiple try-catch blocks');
    }
  } catch (e) {
    console.log('FAIL: Multiple try-catch - Exception');
    failures++;
  }

  // Test 6: Error in async function with return type
  try {
    async function mayFail(shouldFail: boolean): Promise<number> {
      if (shouldFail) {
        throw new Error('Failed');
      }
      return 42;
    }
    const val = await mayFail(false);
    let caught = false;
    try {
      await mayFail(true);
    } catch (e) {
      caught = true;
    }
    if (val !== 42 || !caught) {
      console.log('FAIL: Conditional error handling incorrect');
      failures++;
    } else {
      console.log('PASS: Conditional error in async function');
    }
  } catch (e) {
    console.log('FAIL: Conditional error - Exception');
    failures++;
  }

  // Test 7: Catch and rethrow
  try {
    async function catchAndRethrow(): Promise<number> {
      try {
        await Promise.reject('inner error');
      } catch (e) {
        throw new Error('rethrown');
      }
      return 0;
    }
    let caught = false;
    try {
      await catchAndRethrow();
    } catch (e) {
      caught = true;
    }
    if (!caught) {
      console.log('FAIL: Rethrown error not caught');
      failures++;
    } else {
      console.log('PASS: Catch and rethrow');
    }
  } catch (e) {
    console.log('FAIL: Catch and rethrow - Exception');
    failures++;
  }

  // Test 8: Error with finally and return
  try {
    async function errorWithFinally(): Promise<number> {
      try {
        throw new Error('test');
      } finally {
        // Finally executes even with error
      }
      return 0;
    }
    let caught = false;
    try {
      await errorWithFinally();
    } catch (e) {
      caught = true;
    }
    if (!caught) {
      console.log('FAIL: Error with finally not caught');
      failures++;
    } else {
      console.log('PASS: Error with finally block');
    }
  } catch (e) {
    console.log('FAIL: Error with finally - Exception');
    failures++;
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
