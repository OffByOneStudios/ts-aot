# Epic 61: "ts-grep" CLI Tool (Startup & IO)

**Status:** Completed
**Goal:** Build a fast, responsive command-line tool to demonstrate startup time and async I/O efficiency.

## Concept
CLI tools written in Node.js often suffer from noticeable startup lag (loading the runtime, parsing the script). `ts-aot` binaries should start instantly.

## Implementation Details

### The TypeScript Code
```typescript
import * as fs from 'fs';
import * as path from 'path';

async function search(dir: string, pattern: string) {
    const entries = await fs.promises.readdir(dir, { withFileTypes: true });
    for (const entry of entries) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            await search(fullPath, pattern);
        } else {
            const content = await fs.promises.readFile(fullPath, 'utf-8');
            if (content.includes(pattern)) {
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
- [x] Create `examples/ts-grep/`.
- [x] Implement recursive directory traversal using `fs.promises`.
- [x] Implement string matching (using `includes` for now).
- [x] Handle command line arguments via `process.argv`.

## Benchmarking Results (Averaged over 5 runs)
- **Target:** `src/` directory (recursive)
- **Pattern:** "llvm"

| Metric | Node.js (V8) | ts-aot | Speedup |
| :--- | :--- | :--- | :--- |
| **Startup (no args)** | 0.0333s | 0.0115s | **2.91x** |
| **Runtime (search)** | 0.0490s | 0.0320s | **1.53x** |

*Note: ts-aot shows a significant advantage in both startup and runtime, even for I/O bound tasks.*
