/**
 * Creates a debounced function that delays invoking `fn` until after `wait`
 * milliseconds have elapsed since the last time it was invoked.
 * 
 * @example
 * const debouncedSearch = debounce(() => search(input.value), 300);
 * input.addEventListener('keyup', debouncedSearch);
 */
export function debounce(fn: () => void, wait: number): () => void {
    let timeoutId: number = -1;
    return (): void => {
        if (timeoutId !== -1) {
            clearTimeout(timeoutId);
        }
        timeoutId = setTimeout(fn, wait);
    };
}
