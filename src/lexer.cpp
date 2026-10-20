#include "lexer.h"
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace jocky {

namespace {

const std::unordered_map<std::string_view, Tok>& keywords() {
    static const std::unordered_map<std::string_view, Tok> kw = {
        {"fn", Tok::KwFn},         {"struct", Tok::KwStruct},
        {"import", Tok::KwImport}, {"if", Tok::KwIf},
        {"else", Tok::KwElse},     {"while", Tok::KwWhile},
        {"for", Tok::KwFor},       {"in", Tok::KwIn},
        {"return", Tok::KwReturn}, {"const", Tok::KwConst},
        {"auto", Tok::KwAuto},     {"true", Tok::KwTrue},
        {"false", Tok::KwFalse},   {"nil", Tok::KwNil},
        // built-in type names
        {"int", Tok::KwInt},       {"float", Tok::KwFloat},
        {"byte", Tok::KwByte},     {"bool", Tok::KwBool},
        {"void", Tok::KwVoid},     {"string", Tok::KwString},
        {"bytes", Tok::KwBytes},   {"list", Tok::KwList},
        {"map", Tok::KwMap},
    };
    return kw;
}

bool isIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool isIdentCont(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
bool isHexDigit(char c) { return std::isxdigit((unsigned char)c); }

} // namespace

Lexer::Lexer(std::string_view src, std::string filename)
    : src_(src), file_(std::move(filename)) {}

char Lexer::peek(int ahead) const {
    size_t i = pos_ + ahead;
    return i < src_.size() ? src_[i] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (atEnd() || src_[pos_] != expected) return false;
    advance();
    return true;
}

Token Lexer::makeToken(Tok kind) {
    Token t;
    t.kind = kind;
    t.text = std::string(src_.substr(tokStart_, pos_ - tokStart_));
    t.line = tokLine_;
    t.col = tokCol_;
    return t;
}

Token Lexer::errorToken(std::string code, std::string message) {
    diags_.push_back({code, message, tokLine_, tokCol_});
    Token t;
    t.kind = Tok::Error;
    t.text = std::move(message);
    t.line = tokLine_;
    t.col = tokCol_;
    return t;
}

void Lexer::skipTrivia() {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (!atEnd() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            uint32_t startLine = line_, startCol = col_;
            advance(); // '/'
            advance(); // '*'
            // C-style: block comments do NOT nest.
            bool closed = false;
            while (!atEnd()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                diags_.push_back({"E0103", "unterminated block comment",
                                  startLine, startCol});
            }
        } else {
            return;
        }
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    for (;;) {
        skipTrivia();
        tokStart_ = pos_;
        tokLine_ = line_;
        tokCol_ = col_;

        if (atEnd()) {
            out.push_back(makeToken(Tok::Eof));
            return out;
        }

        char c = peek();

        // x"..." hex byte literal (before identifier handling)
        if (c == 'x' && peek(1) == '"') {
            out.push_back(lexHexBytes());
            continue;
        }
        if (isIdentStart(c)) {
            out.push_back(lexIdentOrKeyword());
            continue;
        }
        if (std::isdigit((unsigned char)c)) {
            out.push_back(lexNumber());
            continue;
        }
        if (c == '"') {
            out.push_back(lexString());
            continue;
        }
        if (c == '\'') {
            out.push_back(lexChar());
            continue;
        }

        // Punctuation / operators
        advance();
        switch (c) {
            case '(': out.push_back(makeToken(Tok::LParen)); break;
            case ')': out.push_back(makeToken(Tok::RParen)); break;
            case '{': out.push_back(makeToken(Tok::LBrace)); break;
            case '}': out.push_back(makeToken(Tok::RBrace)); break;
            case '[': out.push_back(makeToken(Tok::LBracket)); break;
            case ']': out.push_back(makeToken(Tok::RBracket)); break;
            case ';': out.push_back(makeToken(Tok::Semicolon)); break;
            case ',': out.push_back(makeToken(Tok::Comma)); break;
            case ':': out.push_back(makeToken(Tok::Colon)); break;
            case '.': out.push_back(makeToken(Tok::Dot)); break;
            case '~': out.push_back(makeToken(Tok::Tilde)); break;
            case '+': out.push_back(makeToken(match('=') ? Tok::PlusEq : Tok::Plus)); break;
            case '-':
                if (match('>')) out.push_back(makeToken(Tok::Arrow));
                else out.push_back(makeToken(match('=') ? Tok::MinusEq : Tok::Minus));
                break;
            case '*': out.push_back(makeToken(match('=') ? Tok::StarEq : Tok::Star)); break;
            case '/': out.push_back(makeToken(match('=') ? Tok::SlashEq : Tok::Slash)); break;
            case '%': out.push_back(makeToken(match('=') ? Tok::PercentEq : Tok::Percent)); break;
            case '!': out.push_back(makeToken(match('=') ? Tok::NotEq : Tok::Bang)); break;
            case '=': out.push_back(makeToken(match('=') ? Tok::EqEq : Tok::Assign)); break;
            case '^': out.push_back(makeToken(match('=') ? Tok::XorEq : Tok::Caret)); break;
            case '<':
                if (match('<')) out.push_back(makeToken(match('=') ? Tok::ShlEq : Tok::Shl));
                else out.push_back(makeToken(match('=') ? Tok::LtEq : Tok::Lt));
                break;
            case '>':
                if (match('>')) out.push_back(makeToken(match('=') ? Tok::ShrEq : Tok::Shr));
                else out.push_back(makeToken(match('=') ? Tok::GtEq : Tok::Gt));
                break;
            case '&':
                if (match('&')) out.push_back(makeToken(Tok::AmpAmp));
                else out.push_back(makeToken(match('=') ? Tok::AndEq : Tok::Amp));
                break;
            case '|':
                if (match('|')) out.push_back(makeToken(Tok::PipePipe));
                else out.push_back(makeToken(match('=') ? Tok::OrEq : Tok::Pipe));
                break;
            default:
                out.push_back(errorToken(
                    "E0100", std::string("unexpected character '") + c + "'"));
                break;
        }
    }
}

Token Lexer::lexIdentOrKeyword() {
    while (isIdentCont(peek())) advance();
    std::string_view word = src_.substr(tokStart_, pos_ - tokStart_);
    auto it = keywords().find(word);
    if (it != keywords().end()) return makeToken(it->second);
    return makeToken(Tok::Ident);
}

Token Lexer::lexNumber() {
    // Prefixed integer bases: 0x / 0b. Underscores allowed between digits.
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        advance(); advance(); // 0x
        std::string digits;
        while (isHexDigit(peek()) || peek() == '_') {
            char d = advance();
            if (d != '_') digits += d;
        }
        if (digits.empty())
            return errorToken("E0101", "hex literal has no digits");
        Token t = makeToken(Tok::IntLit);
        t.intVal = (int64_t)std::strtoull(digits.c_str(), nullptr, 16);
        return t;
    }
    if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
        advance(); advance(); // 0b
        std::string digits;
        while (peek() == '0' || peek() == '1' || peek() == '_') {
            char d = advance();
            if (d != '_') digits += d;
        }
        if (digits.empty())
            return errorToken("E0101", "binary literal has no digits");
        Token t = makeToken(Tok::IntLit);
        t.intVal = (int64_t)std::strtoull(digits.c_str(), nullptr, 2);
        return t;
    }

    // Decimal integer or float.
    std::string digits;
    bool isFloat = false;
    auto eatDigits = [&]() {
        while (std::isdigit((unsigned char)peek()) || peek() == '_') {
            char d = advance();
            if (d != '_') digits += d;
        }
    };
    eatDigits();
    // Fractional part: a '.' followed by a digit (so `a.b` method calls and
    // `1.method()` stay unambiguous — a dot not followed by a digit is a Dot).
    if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
        isFloat = true;
        digits += advance(); // '.'
        eatDigits();
    }
    // Exponent.
    if (peek() == 'e' || peek() == 'E') {
        char n1 = peek(1);
        char n2 = peek(2);
        bool signedExp = (n1 == '+' || n1 == '-');
        if ((std::isdigit((unsigned char)n1)) ||
            (signedExp && std::isdigit((unsigned char)n2))) {
            isFloat = true;
            digits += advance(); // e/E
            if (signedExp) digits += advance();
            eatDigits();
        }
    }

    if (isFloat) {
        Token t = makeToken(Tok::FloatLit);
        t.floatVal = std::strtod(digits.c_str(), nullptr);
        return t;
    }
    Token t = makeToken(Tok::IntLit);
    // Range checking against i64 happens in the type checker (D4); here we
    // parse as unsigned and reinterpret so the min-int edge case survives.
    t.intVal = (int64_t)std::strtoull(digits.c_str(), nullptr, 10);
    t.text = digits; // normalized (underscores stripped) for range checks
    return t;
}

// Reads the interior of a double-quoted string. Validates escapes and brace
// balance; stores the RAW interior (quotes stripped, not yet unescaped) in
// text. Escape decoding and {expr} interpolation splitting happen in the
// parser. Sets hasInterp when a lone '{' (not '{{') is present.
Token Lexer::lexString() {
    advance(); // opening quote
    std::string raw;
    bool interp = false;
    for (;;) {
        if (atEnd() || peek() == '\n')
            return errorToken("E0102", "unterminated string literal");
        char c = peek();
        if (c == '"') { advance(); break; }
        if (c == '\\') {
            raw += advance(); // backslash
            if (atEnd() || peek() == '\n')
                return errorToken("E0102", "unterminated string literal");
            char e = advance();
            raw += e;
            // \xHH needs two hex digits; validate eagerly.
            if (e == 'x') {
                for (int i = 0; i < 2; i++) {
                    if (!isHexDigit(peek()))
                        return errorToken("E0104",
                            "'\\x' escape needs two hex digits");
                    raw += advance();
                }
            }
            continue;
        }
        if (c == '{') {
            if (peek(1) == '{') { raw += advance(); raw += advance(); continue; }
            interp = true;
        }
        if (c == '}' && peek(1) == '}') { raw += advance(); raw += advance(); continue; }
        raw += advance();
    }
    Token t = makeToken(Tok::StringLit);
    t.text = std::move(raw);
    t.hasInterp = interp;
    return t;
}

Token Lexer::lexChar() {
    advance(); // opening quote
    if (atEnd() || peek() == '\n')
        return errorToken("E0105", "unterminated char literal");
    int64_t value;
    if (peek() == '\\') {
        advance();
        char e = advance();
        switch (e) {
            case 'n': value = '\n'; break;
            case 't': value = '\t'; break;
            case 'r': value = '\r'; break;
            case '0': value = 0; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            case '"': value = '"'; break;
            case 'x': {
                char h1 = peek(), h2 = peek(1);
                if (!isHexDigit(h1) || !isHexDigit(h2))
                    return errorToken("E0104", "'\\x' escape needs two hex digits");
                advance(); advance();
                value = (int64_t)std::strtol(std::string({h1, h2}).c_str(), nullptr, 16);
                break;
            }
            default:
                return errorToken("E0106",
                    std::string("unknown escape '\\") + e + "' in char literal");
        }
    } else {
        value = (unsigned char)advance();
    }
    if (!match('\''))
        return errorToken("E0105", "char literal must be a single character");
    Token t = makeToken(Tok::CharLit);
    t.intVal = value;
    return t;
}

// After the main loop confirmed 'x' then '"'. Reads hex digit pairs; ASCII
// whitespace inside is ignored (so pasted YARA strings work). Requires an even
// digit count. Stores uppercased hex digits in text.
Token Lexer::lexHexBytes() {
    advance(); // x
    advance(); // "
    std::string hex;
    for (;;) {
        if (atEnd() || peek() == '\n')
            return errorToken("E0107", "unterminated hex byte literal");
        char c = peek();
        if (c == '"') { advance(); break; }
        if (c == ' ' || c == '\t' || c == '\r') { advance(); continue; }
        if (!isHexDigit(c))
            return errorToken("E0107",
                std::string("invalid hex digit '") + c + "' in x\"...\" literal");
        hex += (char)std::toupper((unsigned char)advance());
    }
    if (hex.size() % 2 != 0)
        return errorToken("E0108", "hex byte literal has an odd number of digits");
    Token t = makeToken(Tok::HexBytes);
    t.text = std::move(hex);
    return t;
}

} // namespace jocky
