/*
 * ICU Data Stub - Satisfies the icudtXX_dat linker symbol without embedding
 * the full ~29MB ICU data set. When linked with this stub instead of icudt.lib,
 * the resulting binary is ~3MB instead of ~32MB.
 *
 * At runtime, ts_icu_init() in Core.cpp detects that u_init() fails (because
 * this stub has INVALID magic bytes), then loads real ICU data from an
 * external icudtXXl.dat file via udata_setCommonData().
 *
 * IMPORTANT: The magic bytes are intentionally INVALID (0x00 0x00 instead of
 * 0xda 0x27). This causes ICU to reject the data immediately during u_init()
 * with a clean error code, rather than crashing while trying to parse a
 * non-existent table of contents.
 */
#include <cstdint>
#include <unicode/uvernum.h>

// Minimal struct that satisfies the linker's need for the icudtXX_dat symbol.
// All zeros = completely invalid ICU data. ICU will reject it immediately.
struct IcuDataStub {
    uint8_t data[32];  // All zeros
};

// Build the symbol name from the ICU major version: icudt74_dat, icudt78_dat, etc.
#define ICU_DAT_SYMBOL_INNER(ver) icudt ## ver ## _dat
#define ICU_DAT_SYMBOL(ver) ICU_DAT_SYMBOL_INNER(ver)

extern "C" {
#if defined(_MSC_VER)
    #define ICU_DAT_INCLUDE_INNER(ver) __pragma(comment(linker, "/INCLUDE:icudt" #ver "_dat"))
    #define ICU_DAT_INCLUDE(ver) ICU_DAT_INCLUDE_INNER(ver)
    ICU_DAT_INCLUDE(U_ICU_VERSION_MAJOR_NUM)
#endif
    extern const IcuDataStub ICU_DAT_SYMBOL(U_ICU_VERSION_MAJOR_NUM) = {{0}};
}
