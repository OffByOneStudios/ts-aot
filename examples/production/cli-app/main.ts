/**
 * Production CLI Application Example
 *
 * A command-line application template demonstrating:
 * - Argument parsing
 * - Command pattern
 * - File system operations
 * - Colored output
 * - Exit codes
 * - Help text
 *
 * Usage:
 *   ts-aot main.ts -o cli.exe
 *   ./cli.exe init myproject
 *   ./cli.exe build
 *   ./cli.exe --help
 */

import { parseArgs, ParsedArgs } from './utils/args';
import { colors } from './utils/console';
import { initCommand } from './commands/init';
import { buildCommand } from './commands/build';
import { helpCommand } from './commands/help';

/**
 * Application metadata
 */
const APP_NAME = 'tsapp';
const APP_VERSION = '1.0.0';
const APP_DESCRIPTION = 'A sample CLI application built with ts-aot';

/**
 * Available commands
 */
const commands: Record<string, (args: ParsedArgs) => number> = {
    init: initCommand,
    build: buildCommand,
    help: helpCommand
};

/**
 * Show version information
 */
function showVersion(): void {
    console.log(`${APP_NAME} version ${APP_VERSION}`);
}

/**
 * Show help message
 */
function showHelp(): void {
    console.log(`${colors.bold}${APP_NAME}${colors.reset} - ${APP_DESCRIPTION}`);
    console.log('');
    console.log(`${colors.bold}Usage:${colors.reset}`);
    console.log(`  ${APP_NAME} <command> [options] [arguments]`);
    console.log('');
    console.log(`${colors.bold}Commands:${colors.reset}`);
    console.log('  init <name>    Initialize a new project');
    console.log('  build          Build the project');
    console.log('  help           Show detailed help for a command');
    console.log('');
    console.log(`${colors.bold}Options:${colors.reset}`);
    console.log('  -h, --help     Show this help message');
    console.log('  -v, --version  Show version information');
    console.log('  -V, --verbose  Enable verbose output');
    console.log('  -q, --quiet    Suppress output');
    console.log('');
    console.log(`${colors.bold}Examples:${colors.reset}`);
    console.log(`  ${APP_NAME} init myproject`);
    console.log(`  ${APP_NAME} build --verbose`);
    console.log(`  ${APP_NAME} help init`);
}

/**
 * Main entry point
 */
function user_main(): number {
    const args = parseArgs(process.argv);

    // Handle global flags
    if (args.flags.has('version') || args.flags.has('v')) {
        showVersion();
        return 0;
    }

    if (args.flags.has('help') || args.flags.has('h')) {
        if (args.positional.length > 0) {
            // Help for specific command
            args.positional.unshift('help');
        } else {
            showHelp();
            return 0;
        }
    }

    // No command provided
    if (args.positional.length === 0) {
        showHelp();
        return 0;
    }

    // Get command
    const commandName = args.positional[0];
    const command = commands[commandName];

    if (!command) {
        console.error(`${colors.red}Error:${colors.reset} Unknown command '${commandName}'`);
        console.error(`Run '${APP_NAME} --help' for available commands.`);
        return 1;
    }

    // Execute command
    try {
        return command(args);
    } catch (err) {
        const error = err as Error;
        console.error(`${colors.red}Error:${colors.reset} ${error.message}`);

        if (args.flags.has('verbose') || args.flags.has('V')) {
            console.error('');
            console.error('Stack trace:');
            console.error(error.stack || 'No stack trace available');
        }

        return 1;
    }
}
