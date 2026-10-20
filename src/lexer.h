#pragma once
#include "diagnostic.h"
#include "token.h"
#include <string>
#include <string_view>
#include <vector>

namespace jocky {

class Lexer {
public:
    // 'src' must outlive the Lexer. 'filename' is used only for diagnostics.
    Lexer(std::string_view src, std::string filename);

    // Tokenizes the whole input. The final token is always Tok::Eof.
    // Lexer errors are recorded in diagnostics() and emitted inline as
    // Tok::Error tokens so the driver can decide whether to continue.
    std::vector<Token> tokenize();

    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    bool hadError() const { return !diags_.empty(); }
    const std::string& filename() const { return file_; }

private:
    char peek(int ahead = 0) const;
    char advance();
    bool match(char expected);
    bool atEnd() const { return pos_ >= src_.size(); }

    void skipTrivia(); // whitespace + comments
    Token makeToken(Tok kind);
    Token errorToken(std::string code, std::string message);

    Token lexNumber();
    Token lexIdentOrKeyword();
    Token lexString();
    Token lexChar();
    Token lexHexBytes(); // after seeing x"

    std::string_view src_;
    std::string file_;
    size_t pos_ = 0;
    size_t tokStart_ = 0;
    uint32_t line_ = 1;
    uint32_t col_ = 1;
    uint32_t tokLine_ = 1;
    uint32_t tokCol_ = 1;
    std::vector<Diagnostic> diags_;
};

} // namespace jocky
