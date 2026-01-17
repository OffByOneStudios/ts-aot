// TsStringDecoder.h - Node.js string_decoder module

#pragma once

#include "TsObject.h"
#include "TsBuffer.h"
#include "TsString.h"

class TsStringDecoder : public TsObject {
public:
    static const uint32_t MAGIC = 0x53544443; // "STDC"

    TsStringDecoder(TsString* encoding = nullptr);

    // Instance methods
    TsString* Write(TsBuffer* buffer);
    TsString* End(TsBuffer* buffer = nullptr);

    // Get encoding
    TsString* GetEncoding() { return encoding; }

private:
    TsString* encoding;
    uint8_t* pendingBytes;  // Incomplete multi-byte sequence
    size_t pendingLength;   // Number of pending bytes
    size_t pendingCapacity; // Capacity of pending buffer

    void appendPending(const uint8_t* bytes, size_t len);
    void clearPending();
};

extern "C" {
    void* ts_string_decoder_create(void* encoding);
    void* ts_string_decoder_write(void* decoder, void* buffer);
    void* ts_string_decoder_end(void* decoder, void* buffer);
    void* ts_string_decoder_get_encoding(void* decoder);
}
