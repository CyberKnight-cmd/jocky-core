#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace jocky {

enum class Tok : uint8_t {
    // Special
    Eof,
    Error, // lexer error sentinel; message carried in Token::text

    // Literals
    IntLit,     // 42, 0x7FFF_FFFF, 0b1010   -> value in intVal
    FloatLit,   // 3.14, 1e9                  -> value in floatVal
    StringLit,  // "..."  (text = raw interior, not yet unescaped; see hasInterp)
    HexBytes,   // x"4D5A90"                  -> text = raw hex digits (even count, validated)
    CharLit,    // 'M'                        -> value in intVal (0..255)

    Ident, // identifiers and user type names

    // Keywords
    KwFn, KwStruct, KwImport, KwIf, KwElse, KwWhile, KwFor, KwIn,
    KwReturn, KwConst, KwAuto, KwTrue, KwFalse, KwNil,

    // Built-in type-name keywords (kept distinct so casts like (int)x parse
    // unambiguously). 'Error' is NOT here: it is a predeclared struct name.
    KwInt, KwFloat, KwByte, KwBool, KwVoid, KwString, KwBytes, KwList, KwMap,

    // Punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Semicolon, Comma, Colon, Dot, Arrow, // ->

    // Operators
    Plus, Minus, Star, Slash, Percent,
    Assign,                                   // =
    PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
    AndEq, OrEq, XorEq, ShlEq, ShrEq,
    EqEq, NotEq, Lt, LtEq, Gt, GtEq,          // == != < <= > >=
    AmpAmp, PipePipe, Bang,                   // && || !
    Amp, Pipe, Caret, Tilde, Shl, Shr,        // & | ^ ~ << >>
};

struct Token {
    Tok kind = Tok::Eof;
    std::string text;      // lexeme or decoded payload (identifiers, strings, hex, errors)
    int64_t intVal = 0;    // IntLit / CharLit
    double floatVal = 0;   // FloatLit
    bool hasInterp = false;// StringLit: contains a {expr} segment

    uint32_t line = 0;     // 1-based
    uint32_t col = 0;      // 1-based, column of first char

    bool is(Tok k) const { return kind == k; }
};

const char* tokName(Tok k);

} // namespace jocky
