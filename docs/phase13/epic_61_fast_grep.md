# Epic 61: "ts-grep" CLI Tool (Startup & IO)

**Status:** Planned
**Goal:** Build a fast, responsive command-line tool to demonstrate startup time and async I/O efficiency.

## Concept
CLI tools written in Node.js often suffer from noticeable startup lag (loading the runtime, parsing the script). `ts-aot` binaries should start instantly.

## Implementation Details

### The TypeScript Code
```typescript
import * as fs from 'fs';
import * as path from 'path';

async function search(dir: string, pattern: RegExp) {
    const entries = await fs.promises.readdir(dir, { withFileTypes: true });
    for (const entry of entries) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            await search(fullPath, pattern);
        } else {
            const content = await fs.promises.readFile(fullPath, 'utf-8');
            if (pattern.test(content)) {
                console.log(fullPath);
            }
        }
    }
}
```

### The AOT Advantage
1.  **Startup:** The binary is loaded directly by the OS. No JIT warmup.
2.  **Libuv Integration:** The generated code calls `uv_fs_*` directly.
3.  **String Handling:** Demonstrates the efficiency of our `TsString` (ICU) implementation in a real-world searching context.

## Tasks
- [ ] Create `examples/ts-grep/`.
- [ ] Implement recursive directory traversal using `fs.promises`.
- [ ] Implement regex matching using `RegExp` (Epic 54).
- [ ] Handle command line arguments via `process.argv`.

## Benchmarking
- Measure "Time to First Result" on a large codebase (e.g., the `ts-aot` repo itself).
- Measure total execution time for a full scan.
