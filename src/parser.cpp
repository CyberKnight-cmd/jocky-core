#include "parser.h"
#include <cstdlib>

namespace jocky {

// ---- operator tables -------------------------------------------------------
namespace {
struct BinInfo { BinOp op; int prec; bool valid; };

BinInfo binInfo(Tok k) {
    switch (k) {
        case Tok::PipePipe: return {BinOp::Or, 1, true};
        case Tok::AmpAmp:   return {BinOp::And, 2, true};
        case Tok::Pipe:     return {BinOp::BitOr, 3, true};
        case Tok::Caret:    return {BinOp::BitXor, 4, true};
        case Tok::Amp:      return {BinOp::BitAnd, 5, true};
        case Tok::EqEq:     return {BinOp::Eq, 6, true};
        case Tok::NotEq:    return {BinOp::Ne, 6, true};
        case Tok::Lt:       return {BinOp::Lt, 7, true};
        case Tok::LtEq:     return {BinOp::Le, 7, true};
        case Tok::Gt:       return {BinOp::Gt, 7, true};
        case Tok::GtEq:     return {BinOp::Ge, 7, true};
        case Tok::Shl:      return {BinOp::Shl, 8, true};
        case Tok::Shr:      return {BinOp::Shr, 8, true};
        case Tok::Plus:     return {BinOp::Add, 9, true};
        case Tok::Minus:    return {BinOp::Sub, 9, true};
        case Tok::Star:     return {BinOp::Mul, 10, true};
        case Tok::Slash:    return {BinOp::Div, 10, true};
        case Tok::Percent:  return {BinOp::Mod, 10, true};
        default:            return {BinOp::Add, 0, false};
    }
}

bool isTypeKeyword(Tok k) {
    switch (k) {
        case Tok::KwInt: case Tok::KwFloat: case Tok::KwByte: case Tok::KwBool:
        case Tok::KwVoid: case Tok::KwString: case Tok::KwBytes:
        case Tok::KwList: case Tok::KwMap:
            return true;
        default: return false;
    }
}

bool startsPrimary(Tok k) {
    switch (k) {
        case Tok::Ident: case Tok::IntLit: case Tok::FloatLit:
        case Tok::StringLit: case Tok::HexBytes: case Tok::CharLit:
        case Tok::KwTrue: case Tok::KwFalse: case Tok::KwNil:
        case Tok::LParen:
            return true;
        default: return false;
    }
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
} // namespace

const char* binOpName(BinOp op) {
    switch (op) {
        case BinOp::Add: return "+"; case BinOp::Sub: return "-";
        case BinOp::Mul: return "*"; case BinOp::Div: return "/";
        case BinOp::Mod: return "%"; case BinOp::Eq: return "==";
        case BinOp::Ne: return "!="; case BinOp::Lt: return "<";
        case BinOp::Le: return "<="; case BinOp::Gt: return ">";
        case BinOp::Ge: return ">="; case BinOp::And: return "&&";
        case BinOp::Or: return "||"; case BinOp::BitAnd: return "&";
        case BinOp::BitOr: return "|"; case BinOp::BitXor: return "^";
        case BinOp::Shl: return "<<"; case BinOp::Shr: return ">>";
    }
    return "?";
}
const char* unOpName(UnOp op) {
    switch (op) {
        case UnOp::Neg: return "-"; case UnOp::Not: return "!";
        case UnOp::BitNot: return "~";
    }
    return "?";
}

// ---- cursor ----------------------------------------------------------------
Parser::Parser(std::vector<Token> toks, std::string filename)
    : toks_(std::move(toks)), file_(std::move(filename)) {}

const Token& Parser::peek(int ahead) const {
    size_t i = pos_ + ahead;
    if (i >= toks_.size()) return toks_.back(); // Eof
    return toks_[i];
}
bool Parser::match(Tok k) {
    if (check(k)) { advance(); return true; }
    return false;
}
const Token& Parser::advance() {
    const Token& t = toks_[pos_];
    if (pos_ + 1 < toks_.size()) pos_++;
    return t;
}
void Parser::error(const Token& at, std::string code, std::string msg) {
    diags_.push_back({std::move(code), std::move(msg), at.line, at.col});
}
void Parser::fail(const Token& at, std::string code, std::string msg) {
    error(at, std::move(code), std::move(msg));
    throw ParseError{};
}
const Token& Parser::expect(Tok k, const char* what) {
    if (check(k)) return advance();
    fail(cur(), "E0200",
         std::string("expected ") + what + " but found '" + tokName(cur().kind) + "'");
}

void Parser::synchronize() {
    // Skip to the next statement/decl boundary so one error doesn't cascade.
    while (!check(Tok::Eof)) {
        if (toks_[pos_ > 0 ? pos_ - 1 : 0].is(Tok::Semicolon)) return;
        switch (cur().kind) {
            case Tok::KwFn: case Tok::KwStruct: case Tok::KwImport:
            case Tok::KwIf: case Tok::KwWhile: case Tok::KwFor:
            case Tok::KwReturn: case Tok::KwConst: case Tok::RBrace:
                return;
            default: advance();
        }
    }
}

// ---- program ---------------------------------------------------------------
Program Parser::parseProgram() {
    Program prog;
    while (!check(Tok::Eof)) {
        try {
            if (check(Tok::KwImport)) {
                parseImport(prog);
            } else if (check(Tok::KwStruct)) {
                prog.structs.push_back(parseStruct());
            } else if (check(Tok::KwFn)) {
                prog.functions.push_back(parseFn());
            } else if (check(Tok::KwConst)) {
                prog.globals.push_back(
                    std::unique_ptr<VarDeclStmt>(
                        static_cast<VarDeclStmt*>(parseVarDecl(true).release())));
            } else {
                fail(cur(), "E0201",
                     std::string("expected a top-level declaration (import/struct/fn/const), found '") +
                         tokName(cur().kind) + "'");
            }
        } catch (ParseError&) {
            synchronize();
        }
    }
    return prog;
}

void Parser::parseImport(Program& prog) {
    Loc l = loc();
    expect(Tok::KwImport, "'import'");
    const Token& name = expect(Tok::Ident, "module name");
    expect(Tok::Semicolon, "';'");
    prog.imports.push_back({name.text, l});
}

std::unique_ptr<StructDecl> Parser::parseStruct() {
    auto s = std::make_unique<StructDecl>();
    s->loc = loc();
    expect(Tok::KwStruct, "'struct'");
    s->name = expect(Tok::Ident, "struct name").text;
    expect(Tok::LBrace, "'{'");
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
        Field f;
        f.loc = loc();
        f.type = parseType();
        f.name = expect(Tok::Ident, "field name").text;
        expect(Tok::Semicolon, "';'");
        s->fields.push_back(std::move(f));
    }
    expect(Tok::RBrace, "'}'");
    return s;
}

std::unique_ptr<FnDecl> Parser::parseFn() {
    auto fn = std::make_unique<FnDecl>();
    fn->loc = loc();
    expect(Tok::KwFn, "'fn'");
    fn->name = expect(Tok::Ident, "function name").text;
    expect(Tok::LParen, "'('");
    if (!check(Tok::RParen)) {
        do {
            Param p;
            p.loc = loc();
            p.type = parseType();
            p.name = expect(Tok::Ident, "parameter name").text;
            fn->params.push_back(std::move(p));
        } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "')'");
    if (match(Tok::Arrow)) {
        if (match(Tok::LParen)) { // tuple return: (T1, T2, ...)
            do { fn->returns.push_back(parseType()); } while (match(Tok::Comma));
            expect(Tok::RParen, "')'");
        } else {
            fn->returns.push_back(parseType());
        }
    }
    fn->body = parseBlock();
    return fn;
}

// ---- types -----------------------------------------------------------------
TypeExprPtr Parser::parseType() {
    auto t = std::make_unique<TypeExpr>();
    t->loc = loc();
    if (check(Tok::KwList)) {
        advance();
        expect(Tok::LBracket, "'['");
        t->kind = TypeKind::List;
        t->elem = parseType();
        expect(Tok::RBracket, "']'");
        return t;
    }
    if (check(Tok::KwMap)) {
        advance();
        expect(Tok::LBracket, "'['");
        t->kind = TypeKind::Map;
        t->key = parseType();
        expect(Tok::Comma, "','");
        t->elem = parseType();
        expect(Tok::RBracket, "']'");
        return t;
    }
    t->kind = TypeKind::Named;
    if (isTypeKeyword(cur().kind)) { t->name = tokName(cur().kind); advance(); }
    else if (check(Tok::Ident)) { t->name = advance().text; }
    else fail(cur(), "E0202",
              std::string("expected a type, found '") + tokName(cur().kind) + "'");
    return t;
}

bool Parser::isTypeStart() const {
    return isTypeKeyword(cur().kind) || check(Tok::Ident);
}

// ---- statements ------------------------------------------------------------
Block Parser::parseBlock() {
    Block b;
    b.loc = loc();
    expect(Tok::LBrace, "'{'");
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
        try {
            b.stmts.push_back(parseStmt());
        } catch (ParseError&) {
            synchronize();
        }
    }
    expect(Tok::RBrace, "'}'");
    return b;
}

StmtPtr Parser::parseStmt() {
    switch (cur().kind) {
        case Tok::KwConst: return parseVarDecl(true);
        case Tok::KwAuto:  return parseVarDecl(false);
        case Tok::KwIf:    return parseIf();
        case Tok::KwWhile: return parseWhile();
        case Tok::KwFor:   return parseFor();
        case Tok::KwReturn:return parseReturn();
        case Tok::LBrace: {
            auto b = std::make_unique<BlockStmt>(loc());
            b->block = parseBlock();
            return b;
        }
        default: break;
    }
    // Typed declaration?  'type IDENT ...'  — detect by: type-start followed
    // by an IDENT (name).  list[..]/map[..] also begin declarations.
    if (isTypeKeyword(cur().kind) || check(Tok::KwList) || check(Tok::KwMap)) {
        return parseVarDecl(false);
    }
    if (check(Tok::Ident) && peek(1).is(Tok::Ident)) {
        return parseVarDecl(false);
    }
    return parseSimpleStmt();
}

StmtPtr Parser::parseVarDecl(bool isConst) {
    auto v = std::make_unique<VarDeclStmt>(loc());
    v->isConst = isConst;
    if (isConst) match(Tok::KwConst); // consume if present
    if (check(Tok::KwAuto)) {
        advance(); // 'auto' => inferred, declType stays null
    } else {
        v->declType = parseType();
    }
    v->names.push_back(expect(Tok::Ident, "variable name").text);
    while (match(Tok::Comma))
        v->names.push_back(expect(Tok::Ident, "variable name").text);
    expect(Tok::Assign, "'='");
    v->init = parseExpr();
    expect(Tok::Semicolon, "';'");
    return v;
}

StmtPtr Parser::parseIf() {
    auto s = std::make_unique<IfStmt>(loc());
    expect(Tok::KwIf, "'if'");
    s->cond = parseCondExpr();         // struct-lit '{' handled as block (see parsePrimary)
    s->thenBlk = parseBlock();
    if (match(Tok::KwElse)) {
        s->hasElse = true;
        if (check(Tok::KwIf)) s->elseIf = parseIf();
        else s->elseBlk = parseBlock();
    }
    return s;
}

StmtPtr Parser::parseWhile() {
    auto s = std::make_unique<WhileStmt>(loc());
    expect(Tok::KwWhile, "'while'");
    s->cond = parseCondExpr();
    s->body = parseBlock();
    return s;
}

StmtPtr Parser::parseFor() {
    auto s = std::make_unique<ForStmt>(loc());
    expect(Tok::KwFor, "'for'");
    s->var = expect(Tok::Ident, "loop variable").text;
    expect(Tok::KwIn, "'in'");
    s->iterable = parseCondExpr();
    s->body = parseBlock();
    return s;
}

StmtPtr Parser::parseReturn() {
    auto s = std::make_unique<ReturnStmt>(loc());
    expect(Tok::KwReturn, "'return'");
    if (!check(Tok::Semicolon)) {
        s->values.push_back(parseExpr());
        while (match(Tok::Comma)) s->values.push_back(parseExpr());
    }
    expect(Tok::Semicolon, "';'");
    return s;
}

StmtPtr Parser::parseSimpleStmt() {
    Loc l = loc();
    ExprPtr lhs = parseExpr();
    // assignment?
    Tok k = cur().kind;
    bool compound = false;
    BinOp cop = BinOp::Add;
    switch (k) {
        case Tok::Assign: break;
        case Tok::PlusEq:   compound = true; cop = BinOp::Add; break;
        case Tok::MinusEq:  compound = true; cop = BinOp::Sub; break;
        case Tok::StarEq:   compound = true; cop = BinOp::Mul; break;
        case Tok::SlashEq:  compound = true; cop = BinOp::Div; break;
        case Tok::PercentEq:compound = true; cop = BinOp::Mod; break;
        case Tok::AndEq:    compound = true; cop = BinOp::BitAnd; break;
        case Tok::OrEq:     compound = true; cop = BinOp::BitOr; break;
        case Tok::XorEq:    compound = true; cop = BinOp::BitXor; break;
        case Tok::ShlEq:    compound = true; cop = BinOp::Shl; break;
        case Tok::ShrEq:    compound = true; cop = BinOp::Shr; break;
        default: {
            // expression statement
            auto es = std::make_unique<ExprStmt>(l);
            es->expr = std::move(lhs);
            expect(Tok::Semicolon, "';'");
            return es;
        }
    }
    advance(); // consume the assignment operator
    auto a = std::make_unique<AssignStmt>(l);
    a->target = std::move(lhs);
    a->isCompound = compound;
    a->compound = cop;
    a->value = parseExpr();
    expect(Tok::Semicolon, "';'");
    return a;
}

// ---- expressions -----------------------------------------------------------
ExprPtr Parser::parseExpr() { return parseBinary(1); }

ExprPtr Parser::parseCondExpr() {
    bool saved = allowStructLit_;
    allowStructLit_ = false;
    ExprPtr e = parseBinary(1);
    allowStructLit_ = saved;
    return e;
}

ExprPtr Parser::parseBracketedExpr() {
    bool saved = allowStructLit_;
    allowStructLit_ = true;
    ExprPtr e = parseBinary(1);
    allowStructLit_ = saved;
    return e;
}

ExprPtr Parser::parseBinary(int minPrec) {
    ExprPtr lhs = parseUnary();
    for (;;) {
        BinInfo bi = binInfo(cur().kind);
        if (!bi.valid || bi.prec < minPrec) break;
        Loc l = loc();
        advance();
        ExprPtr rhs = parseBinary(bi.prec + 1); // left-assoc
        auto b = std::make_unique<BinaryExpr>(l);
        b->op = bi.op;
        b->lhs = std::move(lhs);
        b->rhs = std::move(rhs);
        lhs = std::move(b);
    }
    return lhs;
}

ExprPtr Parser::parseUnary() {
    Tok k = cur().kind;
    if (k == Tok::Minus || k == Tok::Bang || k == Tok::Tilde) {
        Loc l = loc();
        advance();
        auto u = std::make_unique<UnaryExpr>(l);
        u->op = (k == Tok::Minus) ? UnOp::Neg : (k == Tok::Bang) ? UnOp::Not : UnOp::BitNot;
        u->operand = parseUnary();
        return u;
    }
    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    ExprPtr e = parsePrimary();
    for (;;) {
        if (check(Tok::Dot)) {
            Loc l = loc();
            advance();
            auto m = std::make_unique<MemberExpr>(l);
            m->object = std::move(e);
            m->field = expect(Tok::Ident, "field or method name").text;
            e = std::move(m);
        } else if (check(Tok::LBracket)) {
            Loc l = loc();
            advance();
            auto ix = std::make_unique<IndexExpr>(l);
            ix->object = std::move(e);
            ix->index = parseBracketedExpr();
            expect(Tok::RBracket, "']'");
            e = std::move(ix);
        } else if (check(Tok::LParen)) {
            Loc l = loc();
            advance();
            auto c = std::make_unique<CallExpr>(l);
            c->callee = std::move(e);
            if (!check(Tok::RParen)) {
                do { c->args.push_back(parseBracketedExpr()); } while (match(Tok::Comma));
            }
            expect(Tok::RParen, "')'");
            e = std::move(c);
        } else {
            break;
        }
    }
    return e;
}

ExprPtr Parser::parsePrimary() {
    Loc l = loc();
    const Token& t = cur();
    switch (t.kind) {
        case Tok::IntLit: {
            advance();
            auto e = std::make_unique<IntLitExpr>(l);
            e->value = t.intVal;
            e->text = t.text;
            return e;
        }
        case Tok::FloatLit: {
            advance();
            auto e = std::make_unique<FloatLitExpr>(l);
            e->value = t.floatVal;
            return e;
        }
        case Tok::CharLit: {
            advance();
            auto e = std::make_unique<CharLitExpr>(l);
            e->value = t.intVal;
            return e;
        }
        case Tok::KwTrue: case Tok::KwFalse: {
            advance();
            auto e = std::make_unique<BoolLitExpr>(l);
            e->value = (t.kind == Tok::KwTrue);
            return e;
        }
        case Tok::KwNil: { advance(); return std::make_unique<NilLitExpr>(l); }
        case Tok::StringLit: { advance(); return makeStringExpr(t); }
        case Tok::HexBytes: {
            advance();
            auto e = std::make_unique<HexBytesLitExpr>(l);
            for (size_t i = 0; i + 1 < t.text.size(); i += 2)
                e->bytes.push_back((uint8_t)((hexVal(t.text[i]) << 4) | hexVal(t.text[i + 1])));
            return e;
        }
        case Tok::LBracket: { // list literal
            advance();
            auto e = std::make_unique<ListLitExpr>(l);
            if (!check(Tok::RBracket)) {
                do { e->elems.push_back(parseBracketedExpr()); } while (match(Tok::Comma));
            }
            expect(Tok::RBracket, "']'");
            return e;
        }
        case Tok::LBrace: { // map literal  { k: v, ... }
            advance();
            auto e = std::make_unique<MapLitExpr>(l);
            if (!check(Tok::RBrace)) {
                do {
                    e->keys.push_back(parseBracketedExpr());
                    expect(Tok::Colon, "':'");
                    e->values.push_back(parseBracketedExpr());
                } while (match(Tok::Comma));
            }
            expect(Tok::RBrace, "'}'");
            return e;
        }
        case Tok::LParen: {
            // cast  (type-keyword) unary   OR   grouping (expr)
            advance();
            if (isTypeKeyword(cur().kind) || check(Tok::KwList) || check(Tok::KwMap)) {
                TypeExprPtr ty = parseType();
                expect(Tok::RParen, "')'");
                auto c = std::make_unique<CastExpr>(l);
                c->target = std::move(ty);
                c->operand = parseUnary();
                return c;
            }
            // (IDENT) <primary>  -> cast to a named (struct/Error) type
            if (check(Tok::Ident) && peek(1).is(Tok::RParen) &&
                startsPrimary(peek(2).kind)) {
                auto ty = std::make_unique<TypeExpr>();
                ty->kind = TypeKind::Named;
                ty->loc = loc();
                ty->name = advance().text;
                expect(Tok::RParen, "')'");
                auto c = std::make_unique<CastExpr>(l);
                c->target = std::move(ty);
                c->operand = parseUnary();
                return c;
            }
            ExprPtr inner = parseBracketedExpr();
            expect(Tok::RParen, "')'");
            return inner;
        }
        case Tok::Ident: {
            // struct literal  Name{ field: val, ... } — suppressed in
            // condition/iterable position so the '{' reads as a block.
            if (allowStructLit_ && peek(1).is(Tok::LBrace)) {
                std::string name = advance().text; // IDENT
                advance();                          // '{'
                auto e = std::make_unique<StructLitExpr>(l);
                e->typeName = name;
                if (!check(Tok::RBrace)) {
                    do {
                        std::string fname = expect(Tok::Ident, "field name").text;
                        expect(Tok::Colon, "':'");
                        e->fieldNames.push_back(fname);
                        e->fieldValues.push_back(parseBracketedExpr());
                    } while (match(Tok::Comma));
                }
                expect(Tok::RBrace, "'}'");
                return e;
            }
            auto e = std::make_unique<IdentExpr>(l);
            e->name = advance().text;
            return e;
        }
        default:
            fail(t, "E0203",
                 std::string("expected an expression, found '") + tokName(t.kind) + "'");
    }
}

// Decode a string token into either a plain StringLitExpr or an
// InterpStringExpr.  Handles escapes, {{ }} and {expr} interpolation.
ExprPtr Parser::makeStringExpr(const Token& strTok) {
    const std::string& raw = strTok.text;
    Loc l = {strTok.line, strTok.col};

    auto decodeEscapeInto = [&](std::string& out, size_t& i) {
        // raw[i] == '\\'
        i++;
        if (i >= raw.size()) return;
        char e = raw[i++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '0': out += '\0'; break;
            case '\\': out += '\\'; break;
            case '"': out += '"'; break;
            case '\'': out += '\''; break;
            case '{': out += '{'; break;
            case '}': out += '}'; break;
            case 'x': {
                if (i + 1 < raw.size()) {
                    out += (char)((hexVal(raw[i]) << 4) | hexVal(raw[i + 1]));
                    i += 2;
                }
                break;
            }
            default: out += e; break;
        }
    };

    if (!strTok.hasInterp) {
        auto e = std::make_unique<StringLitExpr>(l);
        for (size_t i = 0; i < raw.size();) {
            if (raw[i] == '\\') decodeEscapeInto(e->value, i);
            else e->value += raw[i++];
        }
        return e;
    }

    auto e = std::make_unique<InterpStringExpr>(l);
    std::string chunk;
    for (size_t i = 0; i < raw.size();) {
        char c = raw[i];
        if (c == '\\') { decodeEscapeInto(chunk, i); continue; }
        if (c == '{' && i + 1 < raw.size() && raw[i + 1] == '{') { chunk += '{'; i += 2; continue; }
        if (c == '}' && i + 1 < raw.size() && raw[i + 1] == '}') { chunk += '}'; i += 2; continue; }
        if (c == '{') {
            // read expression text until matching '}' (no nested braces in v0.1)
            size_t j = i + 1;
            std::string exprText;
            while (j < raw.size() && raw[j] != '}') exprText += raw[j++];
            if (j >= raw.size()) {
                error(strTok, "E0121", "unterminated '{' in interpolated string");
                break;
            }
            i = j + 1; // past '}'
            e->literals.push_back(chunk);
            chunk.clear();
            // sub-lex + sub-parse the embedded expression
            Lexer sub(exprText, file_);
            std::vector<Token> subToks = sub.tokenize();
            for (const auto& d : sub.diagnostics()) diags_.push_back(d);
            Parser subP(std::move(subToks), file_);
            ExprPtr ex;
            try { ex = subP.parseExpr(); }
            catch (ParseError&) {}
            for (const auto& d : subP.diags_) diags_.push_back(d);
            if (!ex) { ex = std::make_unique<StringLitExpr>(l); }
            e->exprs.push_back(std::move(ex));
            continue;
        }
        chunk += c;
        i++;
    }
    e->literals.push_back(chunk);
    return e;
}

} // namespace jocky
