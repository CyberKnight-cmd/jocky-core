#pragma once
#include "ast.h"
#include "lexer.h"
#include "token.h"
#include <optional>
#include <vector>

namespace jocky {

class Parser {
public:
    Parser(std::vector<Token> toks, std::string filename);

    // Parses a whole translation unit. On error, diagnostics() is non-empty
    // and the returned Program may be partial.
    Program parseProgram();

    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    bool hadError() const { return !diags_.empty(); }

private:
    // Token cursor helpers
    const Token& peek(int ahead = 0) const;
    const Token& cur() const { return peek(0); }
    bool check(Tok k) const { return cur().is(k); }
    bool match(Tok k);
    const Token& advance();
    const Token& expect(Tok k, const char* what);
    Loc loc() const { return {cur().line, cur().col}; }
    [[noreturn]] void fail(const Token& at, std::string code, std::string msg);
    void error(const Token& at, std::string code, std::string msg);
    void synchronize(); // recover to next top-level/stmt boundary

    // Declarations
    void parseImport(Program&);
    std::unique_ptr<StructDecl> parseStruct();
    std::unique_ptr<FnDecl> parseFn();
    std::unique_ptr<VarDeclStmt> parseGlobalConst();

    // Types
    TypeExprPtr parseType();

    // Statements
    Block parseBlock();
    StmtPtr parseStmt();
    StmtPtr parseVarDecl(bool isConst);
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseFor();
    StmtPtr parseReturn();
    StmtPtr parseSimpleStmt(); // assignment or expr-stmt

    // Expressions (precedence climbing)
    ExprPtr parseExpr();
    // Parses an expression with struct-literals (`Name{...}`) disabled at the
    // top level, so a trailing `{` is read as a block. Used for if/while
    // conditions and for-loop iterables (Go/Rust-style rule). Parentheses
    // re-enable struct literals inside.
    ExprPtr parseCondExpr();
    // parseExpr() with struct-literals forced on (inside (), [], call args).
    ExprPtr parseBracketedExpr();
    ExprPtr parseBinary(int minPrec);
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();

    ExprPtr makeStringExpr(const Token& strTok);

    bool isTypeStart() const; // does cur() begin a type/decl?

    std::vector<Token> toks_;
    std::string file_;
    size_t pos_ = 0;
    std::vector<Diagnostic> diags_;
    bool allowStructLit_ = true; // false while parsing a condition/iterable

    struct ParseError {}; // thrown by fail(), caught at statement/decl boundary
};

} // namespace jocky
