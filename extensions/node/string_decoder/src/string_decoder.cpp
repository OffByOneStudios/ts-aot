// String decoder module extension - extern "C" wrappers
// Class implementations stay in tsruntime (TsStringDecoder)

#include "TsStringDecoderExt.h"
#include "TsStringDecoder.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"

extern "C" {

void* ts_string_decoder_create(void* encoding) {
    TsString* enc = nullptr;
    if (encoding) {
        TsValue* val = (TsValue*)encoding;
        if (val->type == ValueType::STRING_PTR) {
            enc = (TsString*)val->ptr_val;
        }
    }

    void* mem = ts_alloc(sizeof(TsStringDecoder));
    TsStringDecoder* decoder = new (mem) TsStringDecoder(enc);
    return ts_value_make_object(decoder);
}

void* ts_string_decoder_write(void* decoderArg, void* bufferArg) {
    void* rawDecoder = ts_value_get_object((TsValue*)decoderArg);
    if (!rawDecoder) rawDecoder = decoderArg;

    TsStringDecoder* decoder = dynamic_cast<TsStringDecoder*>((TsObject*)rawDecoder);
    if (!decoder) {
        return ts_value_make_string(TsString::Create(""));
    }

    TsBuffer* buffer = nullptr;
    if (bufferArg) {
        void* rawBuffer = ts_value_get_object((TsValue*)bufferArg);
        if (!rawBuffer) rawBuffer = bufferArg;
        buffer = dynamic_cast<TsBuffer*>((TsObject*)rawBuffer);
    }

    TsString* result = decoder->Write(buffer);
    return ts_value_make_string(result);
}

void* ts_string_decoder_end(void* decoderArg, void* bufferArg) {
    void* rawDecoder = ts_value_get_object((TsValue*)decoderArg);
    if (!rawDecoder) rawDecoder = decoderArg;

    TsStringDecoder* decoder = dynamic_cast<TsStringDecoder*>((TsObject*)rawDecoder);
    if (!decoder) {
        return ts_value_make_string(TsString::Create(""));
    }

    TsBuffer* buffer = nullptr;
    if (bufferArg) {
        void* rawBuffer = ts_value_get_object((TsValue*)bufferArg);
        if (!rawBuffer) rawBuffer = bufferArg;
        buffer = dynamic_cast<TsBuffer*>((TsObject*)rawBuffer);
    }

    TsString* result = decoder->End(buffer);
    return ts_value_make_string(result);
}

void* ts_string_decoder_get_encoding(void* decoderArg) {
    void* rawDecoder = ts_value_get_object((TsValue*)decoderArg);
    if (!rawDecoder) rawDecoder = decoderArg;

    TsStringDecoder* decoder = dynamic_cast<TsStringDecoder*>((TsObject*)rawDecoder);
    if (!decoder) {
        return ts_value_make_string(TsString::Create("utf8"));
    }

    return ts_value_make_string(decoder->GetEncoding());
}

} // extern "C"
