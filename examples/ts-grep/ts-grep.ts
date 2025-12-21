import * as fs from 'fs';
import * as path from 'path';

async function search(dir: string, pattern: string) {
    const entries = await fs.promises.readdir(dir);
    for (const entry of entries) {
        const fullPath = path.join(dir, entry);
        const stats = await fs.promises.stat(fullPath);
        
        if (stats.isDirectory()) {
            await search(fullPath, pattern);
        } else {
            const content = await fs.promises.readFile(fullPath);
            if (content.indexOf(pattern) !== -1) {
                console.log(fullPath);
            }
        }
    }
}

async function main() {
    const args = process.argv;
    if (args.length < 3) {
        console.log("Usage: ts-grep <pattern> [dir]");
        process.exit(1);
    }

    const pattern = args[2];
    const dir = args.length > 3 ? args[3] : ".";

    await search(dir, pattern);
}

main();
