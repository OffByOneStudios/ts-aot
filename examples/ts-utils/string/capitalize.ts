// Capitalize first letter of string (simplified version)
export function capitalize(str: string): string {
    if (str.length === 0) return str;
    // For now, just return the string as-is since toUpperCase chained call is complex
    return str.toUpperCase();
}
