// Test String.prototype.normalize()

function user_main(): number {
    // Test 1: Default normalization (NFC)
    console.log("Test 1 - Default NFC:");
    // Composed vs decomposed form of e with acute accent
    const composed = "\u00E9";     // e with acute accent (single character)
    const decomposed = "e\u0301";  // e + combining acute accent (two characters)

    console.log("Composed length:");
    console.log(composed.length);   // Should be 1

    console.log("Decomposed length:");
    console.log(decomposed.length); // Should be 2

    console.log("Normalized decomposed length:");
    const normalized = decomposed.normalize();  // NFC by default
    console.log(normalized.length); // Should be 1

    // Test 2: NFD normalization (decompose)
    console.log("Test 2 - NFD:");
    const nfd = composed.normalize("NFD");
    console.log("NFD length:");
    console.log(nfd.length);  // Should be 2

    // Test 3: NFKC normalization (compatibility compose)
    console.log("Test 3 - NFKC:");
    const fiLigature = "\uFB01";  // fi ligature
    const nfkc = fiLigature.normalize("NFKC");
    console.log("Original fi ligature length:");
    console.log(fiLigature.length);  // Should be 1
    console.log("NFKC length:");
    console.log(nfkc.length);  // Should be 2 (f + i)

    // Test 4: NFKD normalization (compatibility decompose)
    console.log("Test 4 - NFKD:");
    const nfkd = composed.normalize("NFKD");
    console.log("NFKD length:");
    console.log(nfkd.length);  // Should be 2

    return 0;
}
