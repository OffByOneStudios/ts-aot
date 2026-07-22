// TsParse.cpp — extern "C" boundary over the native TypeScript/JavaScript
// parser, linked into the RUNTIME so eval()/Function(source) can parse at
// program run time (EVAL-001 Phase 0).
//
// The AST is transient C++ (unique_ptr-owned, plain heap) and is NEVER a GC
// object — the collector must not scan, move, or free it. Callers own the
// returned handle and release it with ts_parse_free. This TU deliberately has
// NO runtime dependencies (no ts_alloc / ts_throw / TsString): the eval
// boundary that converts a parse failure into a JS SyntaxError lives with the
// interpreter, not here, so this object links into any binary.

#include "../../../compiler/parser/Parser.h"

#include <memory>
#include <new>
#include <string>

namespace {

struct TsParseResult {
    std::unique_ptr<ast::Program> program;
    std::string error;
};

} // namespace

// Parse `source` as a Program. `as_module` selects the module goal (else
// script goal, the goal eval'd code uses). Always returns a handle; check
// ts_parse_error() for failure. Never throws across the C boundary.
// Length-aware variant: eval source may contain embedded U+0000 (legal in
// comments/strings, ES 12.4) — the C-string entry below truncates at the
// first NUL byte, turning "/* \0 */" into an unterminated comment.
extern "C" void* ts_parse_program_n(const char* source, size_t len,
                                    const char* file_name, int as_module) {
    auto* res = new (std::nothrow) TsParseResult();
    if (!res) return nullptr;
    if (!source) {
        res->error = "SyntaxError: null source";
        return res;
    }
    try {
        ts::parser::Parser parser;
        if (as_module & 2) parser.setFunctionContextEval();
        res->program = parser.parse(std::string(source, len),
                                    file_name ? std::string(file_name)
                                              : std::string("<eval>"));
        if (!res->program) res->error = "SyntaxError: parse produced no program";
    } catch (const std::exception& e) {
        res->error = e.what();
        if (res->error.empty()) res->error = "SyntaxError: invalid source";
    } catch (...) {
        res->error = "SyntaxError: invalid source";
    }
    return res;
}

extern "C" void* ts_parse_program(const char* source, const char* file_name,
                                  int as_module) {
    auto* res = new (std::nothrow) TsParseResult();
    if (!res) return nullptr;
    if (!source) {
        res->error = "SyntaxError: null source";
        return res;
    }
    // as_module bit0: module goal (unused yet — script goal is the default).
    // bit1: FUNCTION-context eval — new.target / super-property are valid at
    // the eval program's toplevel (field/param-initializer direct eval).
    try {
        ts::parser::Parser parser;
        if (as_module & 2) parser.setFunctionContextEval();
        res->program = parser.parse(std::string(source),
                                    file_name ? std::string(file_name)
                                              : std::string("<eval>"));
        if (!res->program) res->error = "SyntaxError: parse produced no program";
    } catch (const std::exception& e) {
        res->error = e.what();
        if (res->error.empty()) res->error = "SyntaxError: invalid source";
    } catch (...) {
        res->error = "SyntaxError: invalid source";
    }
    return res;
}

// Non-empty UTF-8 message if the parse failed, nullptr on success. The
// pointer is owned by the handle — copy it out before ts_parse_free (and in
// particular into GC memory BEFORE any ts_throw longjmp).
extern "C" const char* ts_parse_error(void* handle) {
    auto* res = static_cast<TsParseResult*>(handle);
    if (!res) return "SyntaxError: parser out of memory";
    return res->error.empty() ? nullptr : res->error.c_str();
}

// The parsed ts::ast::Program*, or nullptr if the parse failed. Owned by the
// handle; valid until ts_parse_free.
extern "C" void* ts_parse_get_program(void* handle) {
    auto* res = static_cast<TsParseResult*>(handle);
    return res ? static_cast<void*>(res->program.get()) : nullptr;
}

extern "C" void ts_parse_free(void* handle) {
    delete static_cast<TsParseResult*>(handle);
}
