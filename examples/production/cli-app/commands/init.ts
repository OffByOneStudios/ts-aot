/**
 * Init Command
 *
 * Initialize a new project with a basic structure.
 */

import * as path from 'path';
import { ParsedArgs, hasFlag, getOption } from '../utils/args';
import { colors, success, error, info, step } from '../utils/console';
import { exists, mkdir, writeFile, isDirectory } from '../utils/fs';

/**
 * Package.json template
 */
function packageJsonTemplate(name: string): string {
    return JSON.stringify({
        name: name,
        version: '1.0.0',
        description: '',
        main: 'src/main.ts',
        scripts: {
            build: 'ts-aot src/main.ts -o dist/main.exe',
            start: './dist/main.exe'
        },
        keywords: [],
        author: '',
        license: 'MIT'
    }, null, 2);
}

/**
 * Main.ts template
 */
const mainTsTemplate = `/**
 * Main entry point
 */

function user_main(): number {
    console.log('Hello from ts-aot!');
    return 0;
}
`;

/**
 * README template
 */
function readmeTemplate(name: string): string {
    return `# ${name}

A TypeScript project built with ts-aot.

## Building

\`\`\`bash
ts-aot src/main.ts -o dist/main.exe
\`\`\`

## Running

\`\`\`bash
./dist/main.exe
\`\`\`
`;
}

/**
 * gitignore template
 */
const gitignoreTemplate = `# Build output
dist/
*.exe
*.pdb

# Dependencies
node_modules/

# IDE
.vscode/
.idea/

# OS
.DS_Store
Thumbs.db
`;

/**
 * Init command handler
 */
export function initCommand(args: ParsedArgs): number {
    // Get project name
    const projectName = args.positional[1];

    if (!projectName) {
        error('Missing project name');
        console.log('');
        console.log(`${colors.bold}Usage:${colors.reset} tsapp init <project-name>`);
        console.log('');
        console.log(`${colors.bold}Example:${colors.reset} tsapp init myproject`);
        return 1;
    }

    // Validate project name
    if (!/^[a-zA-Z][a-zA-Z0-9_-]*$/.test(projectName)) {
        error(`Invalid project name '${projectName}'`);
        console.log('Project name must start with a letter and contain only letters, numbers, underscores, and hyphens.');
        return 1;
    }

    const projectPath = path.resolve(projectName);
    const verbose = hasFlag(args, 'verbose', 'V');

    // Check if directory already exists
    if (exists(projectPath) && isDirectory(projectPath)) {
        error(`Directory '${projectName}' already exists`);
        return 1;
    }

    console.log('');
    info(`Creating project '${projectName}'...`);
    console.log('');

    const totalSteps = 6;

    // Step 1: Create project directory
    step(1, totalSteps, 'Creating project directory');
    mkdir(projectPath);

    // Step 2: Create src directory
    step(2, totalSteps, 'Creating src directory');
    mkdir(path.join(projectPath, 'src'));

    // Step 3: Create dist directory
    step(3, totalSteps, 'Creating dist directory');
    mkdir(path.join(projectPath, 'dist'));

    // Step 4: Create package.json
    step(4, totalSteps, 'Creating package.json');
    writeFile(path.join(projectPath, 'package.json'), packageJsonTemplate(projectName));

    // Step 5: Create main.ts
    step(5, totalSteps, 'Creating src/main.ts');
    writeFile(path.join(projectPath, 'src', 'main.ts'), mainTsTemplate);

    // Step 6: Create other files
    step(6, totalSteps, 'Creating README.md and .gitignore');
    writeFile(path.join(projectPath, 'README.md'), readmeTemplate(projectName));
    writeFile(path.join(projectPath, '.gitignore'), gitignoreTemplate);

    console.log('');
    success(`Project '${projectName}' created successfully!`);
    console.log('');
    console.log(`${colors.bold}Next steps:${colors.reset}`);
    console.log(`  cd ${projectName}`);
    console.log('  ts-aot src/main.ts -o dist/main.exe');
    console.log('  ./dist/main.exe');
    console.log('');

    return 0;
}
