#pragma once
#include <cstdint>
#include <string>

namespace jocky {

// A single compiler diagnostic (error/warning), produced by any phase —
// lexer, parser, or code generator. `code` is a stable identifier such as
// "E0007"; see spec/error_codes.md for the registry.
struct Diagnostic {
    std::string code;
    std::string message;
    uint32_t line = 0; // 1-based; 0 means "no specific location"
    uint32_t col = 0;
};

} // namespace jocky
