// Basic HTTP/2 module tests

import * as http2 from 'http2';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: getDefaultSettings returns object with expected properties
    const settings = http2.getDefaultSettings();
    if (settings !== undefined && settings !== null) {
        console.log("PASS: getDefaultSettings returns object");
        passed++;
    } else {
        console.log("FAIL: getDefaultSettings returns null/undefined");
        failed++;
    }

    // Test 2: settings has headerTableSize property
    if (typeof settings.headerTableSize === 'number') {
        console.log("PASS: settings.headerTableSize is number: " + settings.headerTableSize);
        passed++;
    } else {
        console.log("FAIL: settings.headerTableSize not a number");
        failed++;
    }

    // Test 3: settings has enablePush property
    if (typeof settings.enablePush === 'boolean') {
        console.log("PASS: settings.enablePush is boolean: " + settings.enablePush);
        passed++;
    } else {
        console.log("FAIL: settings.enablePush not a boolean");
        failed++;
    }

    // Test 4: settings has maxConcurrentStreams property
    if (typeof settings.maxConcurrentStreams === 'number') {
        console.log("PASS: settings.maxConcurrentStreams is number: " + settings.maxConcurrentStreams);
        passed++;
    } else {
        console.log("FAIL: settings.maxConcurrentStreams not a number");
        failed++;
    }

    // Test 5: settings has initialWindowSize property
    if (typeof settings.initialWindowSize === 'number') {
        console.log("PASS: settings.initialWindowSize is number: " + settings.initialWindowSize);
        passed++;
    } else {
        console.log("FAIL: settings.initialWindowSize not a number");
        failed++;
    }

    // Test 6: settings has maxFrameSize property
    if (typeof settings.maxFrameSize === 'number') {
        console.log("PASS: settings.maxFrameSize is number: " + settings.maxFrameSize);
        passed++;
    } else {
        console.log("FAIL: settings.maxFrameSize not a number");
        failed++;
    }

    // Test 7: settings has maxHeaderListSize property
    if (typeof settings.maxHeaderListSize === 'number') {
        console.log("PASS: settings.maxHeaderListSize is number: " + settings.maxHeaderListSize);
        passed++;
    } else {
        console.log("FAIL: settings.maxHeaderListSize not a number");
        failed++;
    }

    // Test 8: getPackedSettings returns Buffer
    const packed = http2.getPackedSettings(settings);
    if (packed !== undefined && packed !== null) {
        console.log("PASS: getPackedSettings returns buffer");
        passed++;
    } else {
        console.log("FAIL: getPackedSettings returns null/undefined");
        failed++;
    }

    // Test 9: getUnpackedSettings returns object
    const unpacked = http2.getUnpackedSettings(packed);
    if (unpacked !== undefined && unpacked !== null) {
        console.log("PASS: getUnpackedSettings returns object");
        passed++;
    } else {
        console.log("FAIL: getUnpackedSettings returns null/undefined");
        failed++;
    }

    // Test 10: unpacked settings match original
    if (unpacked.headerTableSize === settings.headerTableSize) {
        console.log("PASS: packed/unpacked roundtrip preserves headerTableSize");
        passed++;
    } else {
        console.log("FAIL: packed/unpacked roundtrip lost headerTableSize");
        failed++;
    }

    // Test 11: constants object exists
    const constants = http2.constants;
    if (constants !== undefined && constants !== null) {
        console.log("PASS: http2.constants exists");
        passed++;
    } else {
        console.log("FAIL: http2.constants is null/undefined");
        failed++;
    }

    // Test 12: NGHTTP2_NO_ERROR constant
    if (typeof constants.NGHTTP2_NO_ERROR === 'number' && constants.NGHTTP2_NO_ERROR === 0) {
        console.log("PASS: constants.NGHTTP2_NO_ERROR is 0");
        passed++;
    } else {
        console.log("FAIL: constants.NGHTTP2_NO_ERROR incorrect");
        failed++;
    }

    // Test 13: NGHTTP2_PROTOCOL_ERROR constant
    if (typeof constants.NGHTTP2_PROTOCOL_ERROR === 'number' && constants.NGHTTP2_PROTOCOL_ERROR === 1) {
        console.log("PASS: constants.NGHTTP2_PROTOCOL_ERROR is 1");
        passed++;
    } else {
        console.log("FAIL: constants.NGHTTP2_PROTOCOL_ERROR incorrect");
        failed++;
    }

    // Test 14: sensitiveHeaders symbol exists
    if (http2.sensitiveHeaders !== undefined) {
        console.log("PASS: http2.sensitiveHeaders exists");
        passed++;
    } else {
        console.log("FAIL: http2.sensitiveHeaders is undefined");
        failed++;
    }

    // Print summary
    console.log("");
    console.log("HTTP/2 Basic Tests: " + passed + " passed, " + failed + " failed");

    return failed > 0 ? 1 : 0;
}
