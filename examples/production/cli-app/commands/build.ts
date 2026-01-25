/**
 * Build Command
 *
 * Build the current project.
 */

import * as path from 'path';
import { ParsedArgs, hasFlag, getOption } from '../utils/args';
import { colors, success, error, info, warning, step, spinner } from '../utils/console';
import { exists, isFile, isDirectory, listFiles, fileSize, formatBytes } from '../utils/fs';

/**
 * Find project root (directory with package.json)
 */
function findProjectRoot(startDir: string): string | null {
    let current = startDir;

    while (current !== path.dirname(current)) {
        if (exists(path.join(current, 'package.json'))) {
            return current;
        }
        current = path.dirname(current);
    }

    return null;
}

/**
 * Build command handler
 */
export function buildCommand(args: ParsedArgs): number {
    const verbose = hasFlag(args, 'verbose', 'V');
    const quiet = hasFlag(args, 'quiet', 'q');

    // Find project root
    const cwd = process.cwd();
    const projectRoot = findProjectRoot(cwd);

    if (!projectRoot) {
        error('Not in a project directory (no package.json found)');
        console.log('');
        console.log('Run this command from within a project directory,');
        console.log('or create a new project with: tsapp init <name>');
        return 1;
    }

    if (!quiet) {
        console.log('');
        info(`Building project at ${projectRoot}`);
        console.log('');
    }

    // Check for source files
    const srcDir = path.join(projectRoot, 'src');
    if (!isDirectory(srcDir)) {
        error('No src directory found');
        return 1;
    }

    // Find TypeScript files
    const tsFiles = listFiles(srcDir, true).filter(f => f.endsWith('.ts'));

    if (tsFiles.length === 0) {
        error('No TypeScript files found in src directory');
        return 1;
    }

    if (verbose) {
        console.log(`Found ${tsFiles.length} TypeScript file(s):`);
        for (const file of tsFiles) {
            const relPath = path.relative(projectRoot, file);
            const size = formatBytes(fileSize(file));
            console.log(`  ${relPath} (${size})`);
        }
        console.log('');
    }

    // Find main entry point
    const mainFile = path.join(srcDir, 'main.ts');
    if (!isFile(mainFile)) {
        error('No src/main.ts found');
        console.log('The build command expects src/main.ts as the entry point.');
        return 1;
    }

    // Simulate build process
    const totalSteps = 4;

    if (!quiet) {
        step(1, totalSteps, 'Parsing TypeScript');
    }

    // In a real implementation, this would call ts-aot
    // For demo purposes, we simulate the build steps

    if (!quiet) {
        step(2, totalSteps, 'Type checking');
    }

    if (!quiet) {
        step(3, totalSteps, 'Generating LLVM IR');
    }

    if (!quiet) {
        step(4, totalSteps, 'Linking executable');
    }

    // Report completion
    const outputFile = path.join(projectRoot, 'dist', 'main.exe');

    if (!quiet) {
        console.log('');
        success('Build completed successfully!');
        console.log('');
        console.log(`${colors.bold}Output:${colors.reset} ${outputFile}`);

        if (verbose) {
            console.log('');
            console.log(`${colors.dim}Note: This is a simulated build.${colors.reset}`);
            console.log(`${colors.dim}In production, run: ts-aot ${mainFile} -o ${outputFile}${colors.reset}`);
        }
    }

    return 0;
}
