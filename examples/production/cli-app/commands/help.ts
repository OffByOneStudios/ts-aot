/**
 * Help Command
 *
 * Show detailed help for specific commands.
 */

import { ParsedArgs } from '../utils/args';
import { colors } from '../utils/console';

/**
 * Help for init command
 */
function helpInit(): void {
    console.log(`${colors.bold}tsapp init${colors.reset} - Initialize a new project`);
    console.log('');
    console.log(`${colors.bold}Usage:${colors.reset}`);
    console.log('  tsapp init <project-name> [options]');
    console.log('');
    console.log(`${colors.bold}Arguments:${colors.reset}`);
    console.log('  project-name    Name of the project to create');
    console.log('');
    console.log(`${colors.bold}Options:${colors.reset}`);
    console.log('  -V, --verbose   Show detailed output');
    console.log('');
    console.log(`${colors.bold}Description:${colors.reset}`);
    console.log('  Creates a new project directory with the following structure:');
    console.log('');
    console.log('    project-name/');
    console.log('    ├── package.json');
    console.log('    ├── README.md');
    console.log('    ├── .gitignore');
    console.log('    ├── src/');
    console.log('    │   └── main.ts');
    console.log('    └── dist/');
    console.log('');
    console.log(`${colors.bold}Examples:${colors.reset}`);
    console.log('  tsapp init myproject');
    console.log('  tsapp init my-app --verbose');
}

/**
 * Help for build command
 */
function helpBuild(): void {
    console.log(`${colors.bold}tsapp build${colors.reset} - Build the current project`);
    console.log('');
    console.log(`${colors.bold}Usage:${colors.reset}`);
    console.log('  tsapp build [options]');
    console.log('');
    console.log(`${colors.bold}Options:${colors.reset}`);
    console.log('  -V, --verbose   Show detailed output');
    console.log('  -q, --quiet     Suppress all output');
    console.log('');
    console.log(`${colors.bold}Description:${colors.reset}`);
    console.log('  Compiles the project starting from src/main.ts and outputs');
    console.log('  the executable to dist/main.exe.');
    console.log('');
    console.log('  The command must be run from within a project directory');
    console.log('  (a directory containing package.json).');
    console.log('');
    console.log(`${colors.bold}Examples:${colors.reset}`);
    console.log('  tsapp build');
    console.log('  tsapp build --verbose');
    console.log('  tsapp build -q');
}

/**
 * General help
 */
function helpGeneral(): void {
    console.log(`${colors.bold}tsapp help${colors.reset} - Show help for commands`);
    console.log('');
    console.log(`${colors.bold}Usage:${colors.reset}`);
    console.log('  tsapp help [command]');
    console.log('');
    console.log(`${colors.bold}Available commands:${colors.reset}`);
    console.log('  init    Initialize a new project');
    console.log('  build   Build the current project');
    console.log('  help    Show this help message');
    console.log('');
    console.log(`${colors.bold}Examples:${colors.reset}`);
    console.log('  tsapp help init');
    console.log('  tsapp help build');
}

/**
 * Help command handler
 */
export function helpCommand(args: ParsedArgs): number {
    const topic = args.positional[1];

    console.log('');

    switch (topic) {
        case 'init':
            helpInit();
            break;
        case 'build':
            helpBuild();
            break;
        case 'help':
        case undefined:
            helpGeneral();
            break;
        default:
            console.log(`${colors.red}Error:${colors.reset} Unknown help topic '${topic}'`);
            console.log('');
            helpGeneral();
            return 1;
    }

    console.log('');
    return 0;
}
