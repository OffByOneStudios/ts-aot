/**
 * Creates a throttled function that only invokes `fn` at most once
 * per every `wait` milliseconds.
 * 
 * @example
 * const throttledScroll = throttle(() => updatePosition(), 100);
 * window.addEventListener('scroll', throttledScroll);
 */
export function throttle(fn: () => void, wait: number): () => void {
    let lastCall: number = 0;
    let timeoutId: number = -1;
    return (): void => {
        const now = Date.now();
        const remaining = wait - (now - lastCall);
        if (remaining <= 0) {
            if (timeoutId !== -1) {
                clearTimeout(timeoutId);
                timeoutId = -1;
            }
            lastCall = now;
            fn();
        } else if (timeoutId === -1) {
            timeoutId = setTimeout((): void => {
                lastCall = Date.now();
                timeoutId = -1;
                fn();
            }, remaining);
        }
    };
}
