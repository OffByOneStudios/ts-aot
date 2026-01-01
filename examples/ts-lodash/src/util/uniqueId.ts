/**
 * uniqueId - Generate a unique ID with optional prefix
 * Uses Date.now() + random suffix for uniqueness
 * Note: We avoid module-level mutable state due to compiler limitations
 */

export function uniqueId(prefix: string = ""): string {
    // Use timestamp-based ID for uniqueness
    // In a full implementation, this would use a TsCell-based counter
    const timestamp = Date.now();
    const random = Math.floor(Math.random() * 10000);
    const id = timestamp.toString() + "_" + random.toString();
    if (prefix === "") {
        return id;
    }
    return prefix + id;
}
