#pragma once

#include <string>

namespace tsaot {

// ECMA-262 early-error validation for a regular expression literal.
// `body` and `flags` are the raw source slices between/after the slashes.
// Throws std::runtime_error("line:col: SyntaxError: ...") on an invalid
// flag sequence or a pattern the runtime regex engine (ICU) cannot compile.
void validateRegExpLiteral(const std::string& body, const std::string& flags,
                           int line, int col);

} // namespace tsaot
