/**
 * CLI Tool Startup Benchmark
 *
 * Measures startup time for a realistic CLI application that:
 * - Parses command-line arguments
 * - Performs simple string operations
 * - Produces formatted output
 *
 * This represents the typical workload of a short-lived CLI tool
 * where startup time dominates total execution time.
 *
 * Usage: ./cli_tool.exe [options] [arguments]
 *   --help, -h     Show help
 *   --version, -v  Show version
 *   --count, -c    Count characters in arguments
 *   --upper, -u    Convert to uppercase
 */

interface ParsedArgs {
    help: boolean;
    version: boolean;
    count: boolean;
    upper: boolean;
    args: string[];
}

function parseArgs(argv: string[]): ParsedArgs {
    const result: ParsedArgs = {
        help: false,
        version: false,
        count: false,
        upper: false,
        args: []
    };

    // Skip first arg (exe path) - ts-aot only has exe path at argv[0]
    for (let i = 1; i < argv.length; i++) {
        const arg = argv[i];

        if (arg === '--help' || arg === '-h') {
            result.help = true;
        } else if (arg === '--version' || arg === '-v') {
            result.version = true;
        } else if (arg === '--count' || arg === '-c') {
            result.count = true;
        } else if (arg === '--upper' || arg === '-u') {
            result.upper = true;
        } else if (!arg.startsWith('-')) {
            result.args.push(arg);
        }
    }

    return result;
}

function showHelp(): void {
    console.log('CLI Tool Benchmark');
    console.log('');
    console.log('Usage: cli_tool [options] [arguments]');
    console.log('');
    console.log('Options:');
    console.log('  --help, -h     Show this help message');
    console.log('  --version, -v  Show version information');
    console.log('  --count, -c    Count characters in arguments');
    console.log('  --upper, -u    Convert arguments to uppercase');
    console.log('');
    console.log('Examples:');
    console.log('  cli_tool hello world');
    console.log('  cli_tool -u hello world');
    console.log('  cli_tool -c "some text"');
}

function showVersion(): void {
    console.log('cli_tool version 1.0.0');
    console.log('Built with ts-aot');
}

function processArgs(parsed: ParsedArgs): void {
    if (parsed.args.length === 0) {
        console.log('No arguments provided. Use --help for usage.');
        return;
    }

    let output = parsed.args.join(' ');

    if (parsed.upper) {
        output = output.toUpperCase();
    }

    if (parsed.count) {
        console.log(`Character count: ${output.length}`);
    }

    console.log(output);
}

function user_main(): number {
    const parsed = parseArgs(process.argv);

    if (parsed.help) {
        showHelp();
        return 0;
    }

    if (parsed.version) {
        showVersion();
        return 0;
    }

    processArgs(parsed);

    return 0;
}
