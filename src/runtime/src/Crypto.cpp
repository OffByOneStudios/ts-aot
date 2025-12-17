#include "TsString.h"
#include "md5.h"
#include <sodium.h>

extern "C" {
    void* ts_crypto_md5(void* str) {
        TsString* s = (TsString*)str;
        std::string hash = md5(s->ToUtf8());
        return TsString::Create(hash.c_str());
    }
}
