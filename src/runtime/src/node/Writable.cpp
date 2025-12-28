#include "TsWritable.h"
#include "TsString.h"
#include "TsBuffer.h"
#include "TsRuntime.h"
#include "GC.h"
#include <string.h>

extern "C" {
    bool ts_writable_write(void* writable, void* data) {
        if (!writable || !data) return false;
        TsWritable* w = (TsWritable*)writable;
        
        // Robust check: is it a TsValue* or a raw TsString*/TsBuffer*?
        uint32_t magic = *(uint32_t*)data;
        if (magic == 0x53545247) { // "STRG"
            TsString* str = (TsString*)data;
            return w->Write((void*)str->ToUtf8(), str->Length());
        } else if (magic == 0x42554646) { // "BUFF"
            TsBuffer* buf = (TsBuffer*)data;
            return w->Write(buf->GetData(), buf->GetLength());
        }

        TsValue* v = (TsValue*)data;
        if (v->type == ValueType::STRING_PTR) {
            TsString* str = (TsString*)v->ptr_val;
            return w->Write((void*)str->ToUtf8(), str->Length());
        } else if (v->type == ValueType::OBJECT_PTR) {
            void* obj = v->ptr_val;
            if (obj && *(uint32_t*)obj == 0x42554646) {
                TsBuffer* buf = (TsBuffer*)obj;
                return w->Write(buf->GetData(), buf->GetLength());
            }
        }
        return false;
    }

    void ts_writable_end(void* writable, void* data) {
        if (!writable) return;
        TsWritable* w = (TsWritable*)writable;
        if (data) {
            ts_writable_write(writable, data);
        }
        w->End();
    }
}
