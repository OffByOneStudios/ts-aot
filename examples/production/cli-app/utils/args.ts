/**
 * Argument Parsing Utilities
 */

/**
 * Parsed command line arguments
 */
export interface ParsedArgs {
    /** Positional arguments (non-flag values) */
    positional: string[];
    /** Short and long flags (e.g., -v, --verbose) */
    flags: Set<string>;
    /** Options with values (e.g., --output=file, -o file) */
    options: Map<string, string>;
}

/**
 * Parse command line arguments
 */
export function parseArgs(argv: string[]): ParsedArgs {
    const result: ParsedArgs = {
        positional: [],
        flags: new Set(),
        options: new Map()
    };

    // Skip first two arguments (node/exe and script path)
    let i = 2;

    while (i < argv.length) {
        const arg = argv[i];

        if (arg.startsWith('--')) {
            // Long option
            const eqIndex = arg.indexOf('=');
            if (eqIndex >= 0) {
                // --option=value
                const key = arg.substring(2, eqIndex);
                const value = arg.substring(eqIndex + 1);
                result.options.set(key, value);
            } else {
                // --flag or --option value
                const key = arg.substring(2);

                // Check if next arg is a value
                if (i + 1 < argv.length && !argv[i + 1].startsWith('-')) {
                    result.options.set(key, argv[i + 1]);
                    i++;
                } else {
                    result.flags.add(key);
                }
            }
        } else if (arg.startsWith('-') && arg.length > 1) {
            // Short option(s)
            const flags = arg.substring(1);

            // Handle combined flags like -abc
            for (let j = 0; j < flags.length; j++) {
                const flag = flags[j];

                // Last flag might have a value
                if (j === flags.length - 1 && i + 1 < argv.length && !argv[i + 1].startsWith('-')) {
                    result.options.set(flag, argv[i + 1]);
                    i++;
                } else {
                    result.flags.add(flag);
                }
            }
        } else {
            // Positional argument
            result.positional.push(arg);
        }

        i++;
    }

    return result;
}

/**
 * Get option value with default
 */
export function getOption(args: ParsedArgs, name: string, defaultValue: string = ''): string {
    return args.options.get(name) || defaultValue;
}

/**
 * Check if flag is set
 */
export function hasFlag(args: ParsedArgs, ...names: string[]): boolean {
    for (const name of names) {
        if (args.flags.has(name)) return true;
    }
    return false;
}
