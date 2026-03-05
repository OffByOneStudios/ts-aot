// Test ConsString (rope concat) with non-ASCII / ICU fallback paths.
// Exercises: accented chars, emoji, CJK, mixed ASCII+Unicode concat chains,
// and string methods (indexOf, split, slice, replace, etc.) on concat results.

function user_main(): number {
    let failed = 0;

    // Test 1: Non-ASCII concat (accented characters)
    let accented = "";
    for (let i = 0; i < 100; i++) {
        accented += "\u00E9"; // é (2 bytes in UTF-8)
    }
    if (accented.length !== 100) { console.log("FAIL: accented length " + accented.length); failed++; }
    if (accented.charAt(0) !== "\u00E9") { console.log("FAIL: accented charAt"); failed++; }
    if (accented.charCodeAt(0) !== 233) { console.log("FAIL: accented charCodeAt " + accented.charCodeAt(0)); failed++; }

    // Test 2: Emoji concat (4-byte UTF-8, surrogate pairs in UTF-16)
    const smile = "\u{1F600}";
    let emojis = "";
    for (let i = 0; i < 50; i++) {
        emojis += smile;
    }
    // Each emoji is 2 UTF-16 code units (surrogate pair), so length = 100
    if (emojis.length !== 100) { console.log("FAIL: emoji length " + emojis.length); failed++; }

    // Test 3: CJK concat
    let cjk = "";
    for (let i = 0; i < 100; i++) {
        cjk += "\u4E16\u754C"; // 世界
    }
    if (cjk.length !== 200) { console.log("FAIL: cjk length " + cjk.length); failed++; }
    if (!cjk.startsWith("\u4E16\u754C")) { console.log("FAIL: cjk startsWith"); failed++; }
    if (!cjk.endsWith("\u4E16\u754C")) { console.log("FAIL: cjk endsWith"); failed++; }

    // Test 4: Mixed ASCII + non-ASCII concat
    let mixed = "";
    for (let i = 0; i < 50; i++) {
        mixed += "hello\u00E9world";
    }
    if (mixed.length !== 550) { console.log("FAIL: mixed length " + mixed.length); failed++; }
    if (mixed.indexOf("\u00E9") !== 5) { console.log("FAIL: mixed indexOf " + mixed.indexOf("\u00E9")); failed++; }
    if (!mixed.includes("\u00E9world")) { console.log("FAIL: mixed includes"); failed++; }

    // Test 5: String methods on non-ASCII concat result
    const base = "\u00C0\u00C1\u00C2\u00C3\u00C4"; // ÀÁÂÃÄ
    const suffix = " test \u00E9nd";
    const combined = base + suffix;
    if (combined.indexOf("test") !== 6) { console.log("FAIL: combined indexOf " + combined.indexOf("test")); failed++; }
    if (combined.substring(0, 5) !== "\u00C0\u00C1\u00C2\u00C3\u00C4") { console.log("FAIL: combined substring"); failed++; }
    if (combined.slice(-3) !== "\u00E9nd") { console.log("FAIL: combined slice"); failed++; }

    // Test 6: Split on non-ASCII concat
    let csv = "";
    for (let i = 0; i < 5; i++) {
        if (i > 0) csv += ",";
        csv += "val\u00E9" + i;
    }
    const parts = csv.split(",");
    if (parts.length !== 5) { console.log("FAIL: split length " + parts.length); failed++; }
    if (parts[0] !== "val\u00E90") { console.log("FAIL: split first"); failed++; }
    if (parts[4] !== "val\u00E94") { console.log("FAIL: split last"); failed++; }

    // Test 7: Replace on non-ASCII concat
    let text = "caf\u00E9 " + "au " + "lait";
    const replaced = text.replace("caf\u00E9", "th\u00E9");
    if (!replaced.includes("th\u00E9")) { console.log("FAIL: replace non-ASCII"); failed++; }
    if (replaced.includes("caf\u00E9")) { console.log("FAIL: replace still has old"); failed++; }

    // Test 8: toLowerCase/toUpperCase on non-ASCII concat
    let upper = "CAF\u00C9"; // CAFÉ
    const lower = upper.toLowerCase();
    if (lower !== "caf\u00E9") { console.log("FAIL: toLowerCase non-ASCII"); failed++; }

    // Test 9: Large non-ASCII concat (forces CONS tree + ICU flatten)
    let big = "";
    for (let i = 0; i < 1000; i++) {
        big += "\u00E9";
    }
    if (big.length !== 1000) { console.log("FAIL: big non-ASCII length " + big.length); failed++; }
    if (big.charAt(500) !== "\u00E9") { console.log("FAIL: big non-ASCII charAt"); failed++; }
    if (big.indexOf("\u00E9") !== 0) { console.log("FAIL: big non-ASCII indexOf"); failed++; }
    if (big.lastIndexOf("\u00E9") !== 999) { console.log("FAIL: big non-ASCII lastIndexOf " + big.lastIndexOf("\u00E9")); failed++; }

    // Test 10: Equality across ASCII and non-ASCII concat
    const a = "hello" + " " + "world";
    const b = "hello world";
    if (a !== b) { console.log("FAIL: ASCII equality"); failed++; }

    const c = "caf" + "\u00E9";
    const d = "caf\u00E9";
    if (c !== d) { console.log("FAIL: non-ASCII equality"); failed++; }

    // Test 11: Trim on non-ASCII concat
    let padded = "  " + "\u00E9test\u00E9" + "  ";
    if (padded.trim() !== "\u00E9test\u00E9") { console.log("FAIL: trim non-ASCII"); failed++; }

    // Test 12: Repeat on non-ASCII string
    const repeated = "\u00E9".repeat(10);
    if (repeated.length !== 10) { console.log("FAIL: repeat non-ASCII length"); failed++; }

    // Test 13: padStart with non-ASCII
    const padded2 = "x".padStart(5, "\u00E9");
    if (padded2.length !== 5) { console.log("FAIL: padStart non-ASCII length " + padded2.length); failed++; }
    if (!padded2.startsWith("\u00E9")) { console.log("FAIL: padStart non-ASCII content"); failed++; }

    // Test 14: Normalize on concat result
    const decomposed = "e\u0301"; // e + combining accent
    const normalized = decomposed.normalize("NFC");
    if (normalized !== "\u00E9") { console.log("FAIL: normalize NFC"); failed++; }

    // Test 15: codePointAt on non-ASCII concat
    const emoji2 = "A" + "\u{1F600}" + "B";
    if (emoji2.codePointAt(0) !== 65) { console.log("FAIL: codePointAt ASCII"); failed++; }
    if (emoji2.codePointAt(1) !== 0x1F600) { console.log("FAIL: codePointAt emoji " + emoji2.codePointAt(1)); failed++; }

    if (failed === 0) {
        console.log("All Unicode cons string tests passed!");
    } else {
        console.log("Failed: " + failed);
    }
    return failed;
}
