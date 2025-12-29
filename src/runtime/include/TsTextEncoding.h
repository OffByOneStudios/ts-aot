#pragma once

#include "TsObject.h"
#include "TsString.h"
#include "TsBuffer.h"

// TextEncoder - encodes strings to UTF-8 Uint8Array
class TsTextEncoder : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x54584E43; // "TXNC"
    static TsTextEncoder* Create();
    
    TsBuffer* Encode(TsString* input);
    void* EncodeInto(TsString* source, TsBuffer* destination);
    TsString* GetEncoding() { return encoding; }
    
private:
    TsTextEncoder();
    TsString* encoding;
};

// TextDecoder - decodes Uint8Array to strings
class TsTextDecoder : public TsObject {
public:
    static constexpr uint32_t MAGIC = 0x54584443; // "TXDC"
    static TsTextDecoder* Create(TsString* label = nullptr, bool fatal = false, bool ignoreBOM = false);
    
    TsString* Decode(TsBuffer* input);
    TsString* GetEncoding() { return encoding; }
    bool IsFatal() { return fatal; }
    bool IgnoreBOM() { return ignoreBOM; }
    
private:
    TsTextDecoder(TsString* label, bool fatal, bool ignoreBOM);
    TsString* encoding;
    bool fatal;
    bool ignoreBOM;
};

extern "C" {
    // TextEncoder
    void* ts_text_encoder_create();
    void* ts_text_encoder_encode(void* encoder, void* input);
    void* ts_text_encoder_encode_into(void* encoder, void* source, void* destination);
    void* ts_text_encoder_get_encoding(void* encoder);
    
    // TextDecoder
    void* ts_text_decoder_create(void* label, bool fatal, bool ignoreBOM);
    void* ts_text_decoder_decode(void* decoder, void* input);
    void* ts_text_decoder_get_encoding(void* decoder);
    bool ts_text_decoder_is_fatal(void* decoder);
    bool ts_text_decoder_ignore_bom(void* decoder);
}
