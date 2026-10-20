#include "token.h"

namespace jocky {

const char* tokName(Tok k) {
    switch (k) {
        case Tok::Eof: return "Eof";
        case Tok::Error: return "Error";
        case Tok::IntLit: return "IntLit";
        case Tok::FloatLit: return "FloatLit";
        case Tok::StringLit: return "StringLit";
        case Tok::HexBytes: return "HexBytes";
        case Tok::CharLit: return "CharLit";
        case Tok::Ident: return "Ident";
        case Tok::KwFn: return "fn";
        case Tok::KwStruct: return "struct";
        case Tok::KwImport: return "import";
        case Tok::KwIf: return "if";
        case Tok::KwElse: return "else";
        case Tok::KwWhile: return "while";
        case Tok::KwFor: return "for";
        case Tok::KwIn: return "in";
        case Tok::KwReturn: return "return";
        case Tok::KwConst: return "const";
        case Tok::KwAuto: return "auto";
        case Tok::KwTrue: return "true";
        case Tok::KwFalse: return "false";
        case Tok::KwNil: return "nil";
        case Tok::KwInt: return "int";
        case Tok::KwFloat: return "float";
        case Tok::KwByte: return "byte";
        case Tok::KwBool: return "bool";
        case Tok::KwVoid: return "void";
        case Tok::KwString: return "string";
        case Tok::KwBytes: return "bytes";
        case Tok::KwList: return "list";
        case Tok::KwMap: return "map";
        case Tok::LParen: return "(";
        case Tok::RParen: return ")";
        case Tok::LBrace: return "{";
        case Tok::RBrace: return "}";
        case Tok::LBracket: return "[";
        case Tok::RBracket: return "]";
        case Tok::Semicolon: return ";";
        case Tok::Comma: return ",";
        case Tok::Colon: return ":";
        case Tok::Dot: return ".";
        case Tok::Arrow: return "->";
        case Tok::Plus: return "+";
        case Tok::Minus: return "-";
        case Tok::Star: return "*";
        case Tok::Slash: return "/";
        case Tok::Percent: return "%";
        case Tok::Assign: return "=";
        case Tok::PlusEq: return "+=";
        case Tok::MinusEq: return "-=";
        case Tok::StarEq: return "*=";
        case Tok::SlashEq: return "/=";
        case Tok::PercentEq: return "%=";
        case Tok::AndEq: return "&=";
        case Tok::OrEq: return "|=";
        case Tok::XorEq: return "^=";
        case Tok::ShlEq: return "<<=";
        case Tok::ShrEq: return ">>=";
        case Tok::EqEq: return "==";
        case Tok::NotEq: return "!=";
        case Tok::Lt: return "<";
        case Tok::LtEq: return "<=";
        case Tok::Gt: return ">";
        case Tok::GtEq: return ">=";
        case Tok::AmpAmp: return "&&";
        case Tok::PipePipe: return "||";
        case Tok::Bang: return "!";
        case Tok::Amp: return "&";
        case Tok::Pipe: return "|";
        case Tok::Caret: return "^";
        case Tok::Tilde: return "~";
        case Tok::Shl: return "<<";
        case Tok::Shr: return ">>";
    }
    return "?";
}

} // namespace jocky
