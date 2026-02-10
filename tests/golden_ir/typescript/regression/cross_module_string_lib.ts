export interface Result {
    name: string;
    value: number;
}

export function makeResult(name: string): Result {
    // This function must be complex enough to NOT be inlined.
    // When not inlined, the any-typed string from the caller gets passed
    // as a raw ptr to the string parameter, causing double-boxing in
    // ts_value_make_string if not handled.
    let sum: number = 0.0;
    for (let i = 0; i < 10; i++) {
        sum = sum + i * 0.5;
    }
    return { name: name, value: sum };
}
