// Library for cross_module_typed: numeric-arg functions. These regressed
// silently (weak undefined-returning stub) when the call-site mangling used
// the caller's module hash — string args happened to survive as pointers,
// numeric args produced NaN/undefined.
export function sumSquares(n: number): number {
    let acc: number = 0;
    for (let i: number = 0; i < n; i++) {
        acc = acc + i * i;
    }
    return acc;
}

export function scale(x: number, factor: number): number {
    return x * factor;
}

export function pick(flag: boolean, a: number, b: number): number {
    return flag ? a : b;
}
