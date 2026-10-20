#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace jocky {

// Source location, carried for diagnostics.
struct Loc {
    uint32_t line = 0;
    uint32_t col = 0;
};

// ---------------------------------------------------------------------------
// Type expressions (syntactic — what the programmer wrote)
// ---------------------------------------------------------------------------
enum class TypeKind { Named, List, Map, Auto };

struct TypeExpr {
    TypeKind kind;
    Loc loc;
    std::string name;                 // Named: "int","string","Error","CaseFile",...
    std::unique_ptr<TypeExpr> elem;   // List element, Map value
    std::unique_ptr<TypeExpr> key;    // Map key
};
using TypeExprPtr = std::unique_ptr<TypeExpr>;

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------
enum class BinOp {
    Add, Sub, Mul, Div, Mod,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or,              // && || (short-circuit)
    BitAnd, BitOr, BitXor, Shl, Shr,
};
enum class UnOp { Neg, Not, BitNot };

const char* binOpName(BinOp);
const char* unOpName(UnOp);

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
enum class ExprKind {
    IntLit, FloatLit, BoolLit, NilLit, CharLit,
    StringLit, InterpString, HexBytesLit,
    ListLit, MapLit, StructLit,
    Ident, Unary, Binary, Cast, Call, Member, Index,
};

struct Expr {
    ExprKind kind;
    Loc loc;
    virtual ~Expr() = default;
    explicit Expr(ExprKind k, Loc l) : kind(k), loc(l) {}
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLitExpr : Expr {
    int64_t value = 0;
    std::string text;   // normalized digits, for range checks (D4)
    IntLitExpr(Loc l) : Expr(ExprKind::IntLit, l) {}
};
struct FloatLitExpr : Expr {
    double value = 0;
    FloatLitExpr(Loc l) : Expr(ExprKind::FloatLit, l) {}
};
struct BoolLitExpr : Expr {
    bool value = false;
    BoolLitExpr(Loc l) : Expr(ExprKind::BoolLit, l) {}
};
struct NilLitExpr : Expr { NilLitExpr(Loc l) : Expr(ExprKind::NilLit, l) {} };
struct CharLitExpr : Expr {
    int64_t value = 0; // 0..255
    CharLitExpr(Loc l) : Expr(ExprKind::CharLit, l) {}
};
struct StringLitExpr : Expr {
    std::string value; // decoded
    StringLitExpr(Loc l) : Expr(ExprKind::StringLit, l) {}
};
// Interpolated string: alternating literal chunks and expressions.
// parts[i] is a literal (if exprs slot empty) — we model as two aligned lists.
struct InterpStringExpr : Expr {
    std::vector<std::string> literals;  // size N+1
    std::vector<ExprPtr> exprs;         // size N; rendered between literals
    InterpStringExpr(Loc l) : Expr(ExprKind::InterpString, l) {}
};
struct HexBytesLitExpr : Expr {
    std::vector<uint8_t> bytes;
    HexBytesLitExpr(Loc l) : Expr(ExprKind::HexBytesLit, l) {}
};
struct ListLitExpr : Expr {
    std::vector<ExprPtr> elems;
    ListLitExpr(Loc l) : Expr(ExprKind::ListLit, l) {}
};
struct MapLitExpr : Expr {
    std::vector<ExprPtr> keys;
    std::vector<ExprPtr> values;
    MapLitExpr(Loc l) : Expr(ExprKind::MapLit, l) {}
};
struct StructLitExpr : Expr {
    std::string typeName;
    std::vector<std::string> fieldNames;
    std::vector<ExprPtr> fieldValues;
    StructLitExpr(Loc l) : Expr(ExprKind::StructLit, l) {}
};
struct IdentExpr : Expr {
    std::string name;
    IdentExpr(Loc l) : Expr(ExprKind::Ident, l) {}
};
struct UnaryExpr : Expr {
    UnOp op;
    ExprPtr operand;
    UnaryExpr(Loc l) : Expr(ExprKind::Unary, l) {}
};
struct BinaryExpr : Expr {
    BinOp op;
    ExprPtr lhs, rhs;
    BinaryExpr(Loc l) : Expr(ExprKind::Binary, l) {}
};
struct CastExpr : Expr {
    TypeExprPtr target;
    ExprPtr operand;
    CastExpr(Loc l) : Expr(ExprKind::Cast, l) {}
};
struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
    CallExpr(Loc l) : Expr(ExprKind::Call, l) {}
};
struct MemberExpr : Expr {
    ExprPtr object;
    std::string field;
    MemberExpr(Loc l) : Expr(ExprKind::Member, l) {}
};
struct IndexExpr : Expr {
    ExprPtr object;
    ExprPtr index;
    IndexExpr(Loc l) : Expr(ExprKind::Index, l) {}
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
enum class StmtKind { VarDecl, Assign, ExprStmt, If, While, For, Return, Block };

struct Stmt {
    StmtKind kind;
    Loc loc;
    virtual ~Stmt() = default;
    explicit Stmt(StmtKind k, Loc l) : kind(k), loc(l) {}
};
using StmtPtr = std::unique_ptr<Stmt>;

struct Block {
    std::vector<StmtPtr> stmts;
    Loc loc;
};

struct VarDeclStmt : Stmt {
    bool isConst = false;
    TypeExprPtr declType;             // null => 'auto'
    std::vector<std::string> names;   // >1 for destructuring (auto a, b = f();)
    ExprPtr init;                     // may be null only if single + typed? v0.1 requires init
    VarDeclStmt(Loc l) : Stmt(StmtKind::VarDecl, l) {}
};
struct AssignStmt : Stmt {
    ExprPtr target;        // Ident / Index / Member
    BinOp compound;        // used only if isCompound
    bool isCompound = false; // += -= etc
    ExprPtr value;
    AssignStmt(Loc l) : Stmt(StmtKind::Assign, l) {}
};
struct ExprStmt : Stmt {
    ExprPtr expr;
    ExprStmt(Loc l) : Stmt(StmtKind::ExprStmt, l) {}
};
struct IfStmt : Stmt {
    ExprPtr cond;
    Block thenBlk;
    bool hasElse = false;
    Block elseBlk;             // used if else is a plain block
    StmtPtr elseIf;            // used if 'else if' (an IfStmt); mutually exclusive
    IfStmt(Loc l) : Stmt(StmtKind::If, l) {}
};
struct WhileStmt : Stmt {
    ExprPtr cond;
    Block body;
    WhileStmt(Loc l) : Stmt(StmtKind::While, l) {}
};
struct ForStmt : Stmt {
    std::string var;
    ExprPtr iterable;
    Block body;
    ForStmt(Loc l) : Stmt(StmtKind::For, l) {}
};
struct ReturnStmt : Stmt {
    std::vector<ExprPtr> values;
    ReturnStmt(Loc l) : Stmt(StmtKind::Return, l) {}
};
struct BlockStmt : Stmt {
    Block block;
    BlockStmt(Loc l) : Stmt(StmtKind::Block, l) {}
};

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------
struct Param { TypeExprPtr type; std::string name; Loc loc; };

struct FnDecl {
    std::string name;
    std::vector<Param> params;
    std::vector<TypeExprPtr> returns; // empty or [void] => no value
    Block body;
    Loc loc;
};
struct Field { TypeExprPtr type; std::string name; Loc loc; };
struct StructDecl {
    std::string name;
    std::vector<Field> fields;
    Loc loc;
};
struct ImportDecl { std::string module; Loc loc; };
struct GlobalConst { VarDeclStmt decl; };

struct Program {
    std::vector<ImportDecl> imports;
    std::vector<std::unique_ptr<StructDecl>> structs;
    std::vector<std::unique_ptr<FnDecl>> functions;
    std::vector<std::unique_ptr<VarDeclStmt>> globals; // top-level const
};

// Pretty-printer for --emit-ast
std::string dumpAst(const Program& p);

} // namespace jocky
