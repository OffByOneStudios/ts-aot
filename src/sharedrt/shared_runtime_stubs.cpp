// Definitions for symbols that, in a statically-linked executable, are emitted
// by the ts-aot code generator into the user's object file but are read by the
// runtime. In --shared-runtime mode the runtime lives in this DLL, on the other
// side of the module boundary from the generated exe, so the DLL must provide
// its own definitions.

// ICU data path: HIRToLLVM emits @__ts_icu_data_path with the absolute path to
// icudt74l.dat next to ts-aot.exe (a load-time optimization). In shared mode the
// DLL cannot import that exe-defined symbol, so default it to empty here; the
// runtime then falls back to searching for icudt74l.dat next to the executable /
// DLL / working directory (see ts_icu_init in Core.cpp).
extern "C" const char __ts_icu_data_path[] = "";
