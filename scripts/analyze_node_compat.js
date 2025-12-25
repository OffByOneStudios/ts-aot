const fs = require('fs');
const path = require('path');

const modules = [
    'fs',
    'path',
    'events',
    'stream',
    'util',
    'url',
    'net',
    'http',
    'buffer',
    'process'
];

const output = {};

console.log(`Analyzing Node.js ${process.version} API surface...`);

for (const modName of modules) {
    try {
        const mod = require(modName);
        const keys = Object.keys(mod).sort();
        
        output[modName] = {
            total: keys.length,
            exports: keys.map(key => {
                const type = typeof mod[key];
                return { name: key, type };
            })
        };
    } catch (e) {
        console.error(`Failed to load module ${modName}:`, e.message);
    }
}

// Also analyze global objects
const globals = ['console', 'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval', 'setImmediate', 'clearImmediate', 'TextEncoder', 'TextDecoder'];
output['globals'] = {
    total: globals.length,
    exports: globals.map(name => {
        return { name, type: typeof global[name] };
    })
};

const mdLines = [
    `# Node.js ${process.version} API Baseline`,
    '',
    'This document tracks the feature parity between Node.js LTS and ts-aot.',
    '',
    '| Module | Feature | Type | Status | Notes |',
    '|--------|---------|------|--------|-------|'
];

for (const [modName, data] of Object.entries(output)) {
    for (const item of data.exports) {
        mdLines.push(`| ${modName} | ${item.name} | ${item.type} | 🔴 | |`);
    }
}

const outputPath = path.join(__dirname, '../docs/phase18/compatibility_matrix.md');
fs.writeFileSync(outputPath, mdLines.join('\n'));

console.log(`Generated compatibility matrix at ${outputPath}`);
