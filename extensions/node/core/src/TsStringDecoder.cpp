// TsStringDecoder.cpp - Node.js string_decoder module implementation

#include "TsStringDecoder.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"
#include <cstring>
#include <cstdio>

TsStringDecoder::TsStringDecoder(TsString* enc)
    : encoding(enc ? enc : TsString::Create("utf8"))
    , pendingBytes(nullptr)
    , pendingLength(0)
    , pendingCapacity(0)
{
    magic = MAGIC;
}

void TsStringDecoder::appendPending(const uint8_t* bytes, size_t len) {
    if (len == 0) return;

    size_t newLen = pendingLength + len;
    if (newLen > pendingCapacity) {
        size_t newCap = newLen < 8 ? 8 : newLen * 2;
        uint8_t* newBuf = (uint8_t*)ts_alloc(newCap);
        if (pendingBytes && pendingLength > 0) {
            memcpy(newBuf, pendingBytes, pendingLength);
        }
        pendingBytes = newBuf;
        pendingCapacity = newCap;
    }

    memcpy(pendingBytes + pendingLength, bytes, len);
    pendingLength = newLen;
}

void TsStringDecoder::clearPending() {
    pendingLength = 0;
}

// Check how many bytes are needed to complete a UTF-8 sequence
static int utf8SequenceLength(uint8_t byte) {
    if ((byte & 0x80) == 0) return 1;        // 0xxxxxxx - ASCII
    if ((byte & 0xE0) == 0xC0) return 2;     // 110xxxxx
    if ((byte & 0xF0) == 0xE0) return 3;     // 1110xxxx
    if ((byte & 0xF8) == 0xF0) return 4;     // 11110xxx
    return 1; // Invalid, treat as single byte
}

// Check if a byte is a UTF-8 continuation byte
static bool isContinuationByte(uint8_t byte) {
    return (byte & 0xC0) == 0x80; // 10xxxxxx
}

TsString* TsStringDecoder::Write(TsBuffer* buffer) {
    if (!buffer) {
        return TsString::Create("");
    }

    const char* encStr = "utf8";
    if (encoding) {
        encStr = encoding->ToUtf8();
    }

    // For non-UTF8 encodings, just convert directly
    if (strcmp(encStr, "utf8") != 0 && strcmp(encStr, "utf-8") != 0) {
        // For latin1, ascii, etc., just convert directly
        return buffer->ToString(encoding);
    }

    // UTF-8 handling with multi-byte character preservation
    size_t bufLen = buffer->GetLength();
    const uint8_t* data = buffer->GetData();

    // Combine pending bytes with new data
    size_t totalLen = pendingLength + bufLen;
    if (totalLen == 0) {
        return TsString::Create("");
    }

    uint8_t* combined = (uint8_t*)ts_alloc(totalLen + 1);
    if (pendingLength > 0) {
        memcpy(combined, pendingBytes, pendingLength);
    }
    memcpy(combined + pendingLength, data, bufLen);
    combined[totalLen] = 0;

    // Find the last complete UTF-8 character
    size_t completeLen = totalLen;

    // Check if the last byte(s) are an incomplete sequence
    if (totalLen > 0) {
        // Start from the end and find the start of the last character
        size_t i = totalLen - 1;

        // Skip continuation bytes to find start of last character
        while (i > 0 && isContinuationByte(combined[i])) {
            i--;
        }

        // i now points to the start of the last character
        int expectedLen = utf8SequenceLength(combined[i]);
        int actualLen = totalLen - i;

        if (actualLen < expectedLen) {
            // Incomplete sequence at the end
            completeLen = i;
            // Save the incomplete bytes for next write
            clearPending();
            appendPending(combined + i, totalLen - i);
        } else {
            clearPending();
        }
    }

    if (completeLen == 0) {
        return TsString::Create("");
    }

    // Create string from complete bytes
    combined[completeLen] = 0;
    return TsString::Create((const char*)combined);
}

TsString* TsStringDecoder::End(TsBuffer* buffer) {
    TsString* result;

    if (buffer) {
        // First process the buffer
        result = Write(buffer);
    } else {
        result = TsString::Create("");
    }

    // Then flush any remaining pending bytes
    if (pendingLength > 0) {
        // For the end, we include incomplete sequences (they become replacement chars)
        uint8_t* pending = (uint8_t*)ts_alloc(pendingLength + 1);
        memcpy(pending, pendingBytes, pendingLength);
        pending[pendingLength] = 0;

        TsString* pendingStr = TsString::Create((const char*)pending);

        // Concatenate with result
        if (result && strlen(result->ToUtf8()) > 0) {
            // Concatenate the two strings
            size_t len1 = strlen(result->ToUtf8());
            size_t len2 = strlen(pendingStr->ToUtf8());
            char* combined = (char*)ts_alloc(len1 + len2 + 1);
            strcpy(combined, result->ToUtf8());
            strcat(combined, pendingStr->ToUtf8());
            result = TsString::Create(combined);
        } else {
            result = pendingStr;
        }

        clearPending();
    }

    return result;
}

