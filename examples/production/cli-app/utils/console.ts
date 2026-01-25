/**
 * Console Output Utilities
 *
 * ANSI color codes for terminal output.
 */

/**
 * ANSI color codes
 */
export const colors = {
    // Reset
    reset: '\x1b[0m',

    // Styles
    bold: '\x1b[1m',
    dim: '\x1b[2m',
    italic: '\x1b[3m',
    underline: '\x1b[4m',

    // Foreground colors
    black: '\x1b[30m',
    red: '\x1b[31m',
    green: '\x1b[32m',
    yellow: '\x1b[33m',
    blue: '\x1b[34m',
    magenta: '\x1b[35m',
    cyan: '\x1b[36m',
    white: '\x1b[37m',

    // Bright foreground colors
    brightBlack: '\x1b[90m',
    brightRed: '\x1b[91m',
    brightGreen: '\x1b[92m',
    brightYellow: '\x1b[93m',
    brightBlue: '\x1b[94m',
    brightMagenta: '\x1b[95m',
    brightCyan: '\x1b[96m',
    brightWhite: '\x1b[97m',

    // Background colors
    bgBlack: '\x1b[40m',
    bgRed: '\x1b[41m',
    bgGreen: '\x1b[42m',
    bgYellow: '\x1b[43m',
    bgBlue: '\x1b[44m',
    bgMagenta: '\x1b[45m',
    bgCyan: '\x1b[46m',
    bgWhite: '\x1b[47m'
};

/**
 * Print a success message
 */
export function success(message: string): void {
    console.log(`${colors.green}✓${colors.reset} ${message}`);
}

/**
 * Print an error message
 */
export function error(message: string): void {
    console.error(`${colors.red}✗${colors.reset} ${message}`);
}

/**
 * Print a warning message
 */
export function warning(message: string): void {
    console.log(`${colors.yellow}!${colors.reset} ${message}`);
}

/**
 * Print an info message
 */
export function info(message: string): void {
    console.log(`${colors.blue}ℹ${colors.reset} ${message}`);
}

/**
 * Print a step in a process
 */
export function step(stepNum: number, total: number, message: string): void {
    console.log(`${colors.dim}[${stepNum}/${total}]${colors.reset} ${message}`);
}

/**
 * Print a header
 */
export function header(text: string): void {
    console.log('');
    console.log(`${colors.bold}${text}${colors.reset}`);
    console.log('='.repeat(text.length));
}

/**
 * Print a spinner (static, since we can't animate in sync context)
 */
export function spinner(message: string): void {
    console.log(`${colors.cyan}⟳${colors.reset} ${message}...`);
}
