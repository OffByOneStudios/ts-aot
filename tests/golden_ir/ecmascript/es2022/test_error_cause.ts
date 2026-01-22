// Test ES2022 Error .cause property

function user_main(): number {
    // Basic error without cause
    const err1 = new Error("Basic error");
    console.log(err1.message);
    console.log(err1.cause === undefined ? "no cause" : "has cause");

    // Error with cause (ES2022)
    const originalError = new Error("Original error");
    const wrappedError = new Error("Wrapped error", { cause: originalError });
    console.log(wrappedError.message);
    console.log(wrappedError.cause !== undefined ? "has cause" : "no cause");

    // Access cause's message
    const cause = wrappedError.cause as Error;
    console.log(cause.message);

    // Error with non-Error cause
    const err3 = new Error("Error with string cause", { cause: "string cause" });
    console.log(err3.message);
    console.log(err3.cause);

    // Error with number cause
    const err4 = new Error("Error with number cause", { cause: 42 });
    console.log(err4.message);
    console.log(err4.cause);

    return 0;
}
