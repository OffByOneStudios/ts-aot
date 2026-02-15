declare module 'custom-lib' {
    export function processData(input: string): string;
    export function parseConfig(path: string): object;
    export const VERSION: string;
}
