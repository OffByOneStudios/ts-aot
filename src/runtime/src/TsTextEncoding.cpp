#include "TsTextEncoding.h"
#include "TsRuntime.h"
#include "TsMap.h"
#include <new>
#include <cstring>

// ============================================================================
// TextEncoder Implementation
// ============================================================================

TsTextEncoder::TsTextEncoder() {
    this->magic = MAGIC;
    this->encoding = TsString::Create("utf-8");
}

TsTextEncoder* TsTextEncoder::Create() {
    void* mem = ts_alloc(sizeof(TsTextEncoder));
    return new (mem) TsTextEncoder();
}

TsBuffer* TsTextEncoder::Encode(TsString* input) {
    if (!input) {
        return TsBuffer::Create(0);
    }
    
    // Get UTF-8 representation
    const char* utf8 = input->ToUtf8();
    size_t len = strlen(utf8);
    
    TsBuffer* result = TsBuffer::Create(len);
    memcpy(result->GetData(), utf8, len);
    
    return result;
}

void* TsTextEncoder::EncodeInto(TsString* source, TsBuffer* destination) {
    if (!source || !destination) {
        TsMap* result = TsMap::Create();
        result->Set(*ts_value_make_string(TsString::Create("read")), *ts_value_make_int(0));
        result->Set(*ts_value_make_string(TsString::Create("written")), *ts_value_make_int(0));
        return ts_value_make_object(result);
    }
    
    const char* utf8 = source->ToUtf8();
    size_t srcLen = strlen(utf8);
    size_t destLen = destination->GetLength();
    size_t written = srcLen < destLen ? srcLen : destLen;
    
    memcpy(destination->GetData(), utf8, written);
    
    TsMap* result = TsMap::Create();
    result->Set(*ts_value_make_string(TsString::Create("read")), *ts_value_make_int((int64_t)written));
    result->Set(*ts_value_make_string(TsString::Create("written")), *ts_value_make_int((int64_t)written));
    return ts_value_make_object(result);
}

// ============================================================================
// TextDecoder Implementation
// ============================================================================

TsTextDecoder::TsTextDecoder(TsString* label, bool fatal, bool ignoreBOM) 
    : fatal(fatal), ignoreBOM(ignoreBOM) {
    this->magic = MAGIC;
    
    // Default to UTF-8, but could support other encodings
    if (label) {
        const char* labelStr = label->ToUtf8();
        // Normalize common encoding names
        if (strcmp(labelStr, "utf-8") == 0 || strcmp(labelStr, "utf8") == 0 || 
            strcmp(labelStr, "UTF-8") == 0 || strcmp(labelStr, "UTF8") == 0) {
            this->encoding = TsString::Create("utf-8");
        } else {
            this->encoding = label;
        }
    } else {
        this->encoding = TsString::Create("utf-8");
    }
}

TsTextDecoder* TsTextDecoder::Create(TsString* label, bool fatal, bool ignoreBOM) {
    void* mem = ts_alloc(sizeof(TsTextDecoder));
    return new (mem) TsTextDecoder(label, fatal, ignoreBOM);
}

TsString* TsTextDecoder::Decode(TsBuffer* input) {
    if (!input || input->GetLength() == 0) {
        return TsString::Create("");
    }
    
    // For UTF-8, just create a string from the bytes
    // Note: This assumes valid UTF-8 input
    size_t len = input->GetLength();
    char* buf = (char*)ts_alloc(len + 1);
    memcpy(buf, input->GetData(), len);
    buf[len] = '\0';
    
    // Skip BOM if present and not ignored
    const char* start = buf;
    if (!ignoreBOM && len >= 3) {
        if ((unsigned char)buf[0] == 0xEF && 
            (unsigned char)buf[1] == 0xBB && 
            (unsigned char)buf[2] == 0xBF) {
            start = buf + 3;
        }
    }
    
    return TsString::Create(start);
}

// ============================================================================
// C API
// ============================================================================

extern "C" {

void* ts_text_encoder_create() {
    return TsTextEncoder::Create();
}

void* ts_text_encoder_encode(void* encoder, void* input) {
    TsTextEncoder* enc = (TsTextEncoder*)encoder;
    if (!enc) return TsBuffer::Create(0);
    
    TsString* str = (TsString*)ts_value_get_string((TsValue*)input);
    if (!str) str = (TsString*)input;
    
    return enc->Encode(str);
}

void* ts_text_encoder_encode_into(void* encoder, void* source, void* destination) {
    TsTextEncoder* enc = (TsTextEncoder*)encoder;
    if (!enc) {
        TsMap* result = TsMap::Create();
        result->Set(*ts_value_make_string(TsString::Create("read")), *ts_value_make_int(0));
        result->Set(*ts_value_make_string(TsString::Create("written")), *ts_value_make_int(0));
        return ts_value_make_object(result);
    }
    
    TsString* src = (TsString*)ts_value_get_string((TsValue*)source);
    if (!src) src = (TsString*)source;
    
    TsBuffer* dest = (TsBuffer*)ts_value_get_object((TsValue*)destination);
    if (!dest) dest = (TsBuffer*)destination;
    
    return enc->EncodeInto(src, dest);
}

void* ts_text_encoder_get_encoding(void* encoder) {
    TsTextEncoder* enc = (TsTextEncoder*)encoder;
    if (!enc) return TsString::Create("utf-8");
    return enc->GetEncoding();
}

void* ts_text_decoder_create(void* label, bool fatal, bool ignoreBOM) {
    TsString* labelStr = nullptr;
    if (label) {
        labelStr = (TsString*)ts_value_get_string((TsValue*)label);
        if (!labelStr) labelStr = (TsString*)label;
    }
    return TsTextDecoder::Create(labelStr, fatal, ignoreBOM);
}

void* ts_text_decoder_decode(void* decoder, void* input) {
    TsTextDecoder* dec = (TsTextDecoder*)decoder;
    if (!dec) {
        return TsString::Create("");
    }
    
    TsBuffer* buf = (TsBuffer*)ts_value_get_object((TsValue*)input);
    if (!buf) buf = (TsBuffer*)input;
    
    // Return raw TsString*, not boxed - codegen expects raw string
    return dec->Decode(buf);
}

void* ts_text_decoder_get_encoding(void* decoder) {
    TsTextDecoder* dec = (TsTextDecoder*)decoder;
    if (!dec) return TsString::Create("utf-8");
    return dec->GetEncoding();
}

bool ts_text_decoder_is_fatal(void* decoder) {
    TsTextDecoder* dec = (TsTextDecoder*)decoder;
    return dec ? dec->IsFatal() : false;
}

bool ts_text_decoder_ignore_bom(void* decoder) {
    TsTextDecoder* dec = (TsTextDecoder*)decoder;
    return dec ? dec->IgnoreBOM() : false;
}

}
