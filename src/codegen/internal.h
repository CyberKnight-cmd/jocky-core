// Internal shared state for the code generator.
//
// This header is included only by the src/codegen/*.cpp implementation files.
// It defines CodeGen::Impl — the single object that carries all codegen state
// (LLVM context/module/builder, type tables, scopes) — plus the small helper
// value types. Each concern is implemented in its own .cpp:
//
//   driver.cpp    construction, the top-level run(), target setup, IR/object out
//   lowering.cpp  JOCKY-type -> LLVM-type lowering, coercion, runtime signatures
//   decls.cpp     struct/function/global declaration passes, function skeletons
//   expr.cpp      expression and call emission
//   stmt.cpp      statement emission
//
// Only trivial one-liners live inline here; anything with real logic is
// declared here and defined in the matching .cpp so it stays easy to find.
#pragma once

#include "ast.h"
#include "codegen.h"
#include "diagnostic.h"
#include "types.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Target/TargetMachine.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jocky {

// A typed LLVM value: an SSA value paired with its JOCKY type.
struct TV {
    llvm::Value* val = nullptr;
    Type* type = nullptr;
};

// A variable binding in a scope: its type, its storage (alloca/global), and
// whether it was declared `const`.
struct Sym {
    Type* type;
    llvm::Value* addr;
    bool isConst;
};

// A resolved function: its LLVM function plus JOCKY parameter/return types.
struct FnInfo {
    llvm::Function* fn;
    std::vector<Type*> params;
    std::vector<Type*> returns;
};

// A resolved struct: declaration, LLVM body type, and field layout.
struct StructInfo {
    const StructDecl* decl;
    llvm::StructType* llty;
    std::vector<Type*> fieldTypes;
    std::vector<std::string> fieldNames;
};

// Map key-kind tag understood by the runtime (0 = raw 8-byte, 1 = string).
inline int mapKeyKind(Type* k) { return k->kind == Ty::String ? 1 : 0; }

struct CodeGen::Impl {
    // ---- inputs / outputs ----
    const Program& prog;
    std::string file;
    std::vector<Diagnostic>* diags;

    // ---- LLVM state ----
    llvm::LLVMContext ctx;
    std::unique_ptr<llvm::Module> mod;
    llvm::IRBuilder<> builder;
    TypeContext tc;
    std::unique_ptr<llvm::TargetMachine> tm;

    // cached LLVM types
    llvm::Type *i64, *i32, *i8, *i1, *dbl, *voidTy, *ptrTy;

    // ---- symbol tables ----
    std::unordered_map<std::string, FnInfo> funcs;
    std::unordered_map<std::string, StructInfo> structs;
    std::unordered_set<std::string> modules;       // imported module names
    std::vector<std::unordered_map<std::string, Sym>> scopes;
    std::vector<const VarDeclStmt*> globalDecls;

    // ---- current function being emitted ----
    FnInfo* curFn = nullptr;
    llvm::Function* curLL = nullptr;

    Impl(const Program& p, std::string f, std::vector<Diagnostic>* d)
        : prog(p), file(std::move(f)), diags(d), builder(ctx) {
        mod = std::make_unique<llvm::Module>("jocky", ctx);
        i64 = llvm::Type::getInt64Ty(ctx);
        i32 = llvm::Type::getInt32Ty(ctx);
        i8 = llvm::Type::getInt8Ty(ctx);
        i1 = llvm::Type::getInt1Ty(ctx);
        dbl = llvm::Type::getDoubleTy(ctx);
        voidTy = llvm::Type::getVoidTy(ctx);
        ptrTy = llvm::PointerType::getUnqual(ctx);
    }

    // ---- trivial helpers (kept inline on purpose) ----
    void err(Loc l, const std::string& code, const std::string& msg) {
        diags->push_back({code, msg, l.line, l.col});
    }
    bool ok() const { return diags->empty(); }

    void pushScope() { scopes.emplace_back(); }
    void popScope() { scopes.pop_back(); }
    void declare(const std::string& name, Sym s) { scopes.back()[name] = s; }
    Sym* lookup(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        return nullptr;
    }

    llvm::AllocaInst* entryAlloca(llvm::Type* ty, const std::string& name) {
        llvm::IRBuilder<> tmp(&curLL->getEntryBlock(), curLL->getEntryBlock().begin());
        return tmp.CreateAlloca(ty, nullptr, name);
    }
    bool terminated() {
        llvm::BasicBlock* b = builder.GetInsertBlock();
        return b && !b->empty() && b->back().isTerminator();
    }

    llvm::Value* ci64(int64_t v) { return llvm::ConstantInt::get(i64, v, true); }
    llvm::Value* ci32(int32_t v) { return llvm::ConstantInt::get(i32, v, true); }
    llvm::Value* globalCStr(const std::string& s) { return builder.CreateGlobalString(s); }
    llvm::Value* nilCheck(llvm::Value* ptr, Loc l) {
        builder.CreateCall(rt("jky_nil_check"), {ptr, ci32((int32_t)l.line)});
        return ptr;
    }

    // Runtime function lookup: rt() returns the callee; rtSig() has the ABI.
    llvm::FunctionCallee rt(const char* name) {
        return mod->getOrInsertFunction(name, rtSig(name));
    }

    // ---- driver.cpp ----
    bool initTarget();
    bool run();

    // ---- lowering.cpp ----
    llvm::Type* lower(Type* t);
    llvm::Type* fnRetType(const std::vector<Type*>& rets);
    llvm::FunctionType* rtSig(const std::string& n);
    Type* resolveType(const TypeExpr* te);
    llvm::Value* coerce(TV tv, Type* target, Loc l);
    static bool sameType(Type* a, Type* b);
    Type* concretize(Type* t);
    llvm::Value* toStringValue(TV tv, Loc l);
    llvm::Value* toSlot(TV tv);
    TV fromSlot(llvm::Value* slot, Type* t);

    // ---- decls.cpp ----
    bool declareStructsAndFns();
    void declareGlobals();
    void emitGlobalInit();
    void emitFunction(const FnDecl* fn);

    // ---- expr.cpp ----
    struct CallResult { llvm::Value* raw = nullptr; std::vector<Type*> rets; };
    TV emitExpr(const Expr* e);
    CallResult emitCall(const CallExpr* c);
    CallResult emitModuleCall(const std::string& mod, const std::string& fn, const CallExpr* c);
    CallResult emitMethodCall(TV recv, const std::string& method, const CallExpr* c);
    TV emitBinary(const BinaryExpr* b);
    TV numericOp(TV L, TV R, BinOp op, Loc l);
    TV emitUnary(const UnaryExpr* u);
    TV emitCast(const CastExpr* c);
    TV emitIndex(const IndexExpr* ix);
    TV emitMember(const MemberExpr* m);
    TV emitInterp(const InterpStringExpr* s);
    TV emitListLit(const ListLitExpr* a);
    TV emitMapLit(const MapLitExpr* m);
    TV emitStructLit(const StructLitExpr* s);

    // ---- stmt.cpp ----
    void emitStmt(const Stmt* s);
    void emitVarDecl(const VarDeclStmt* v);
    void emitAssign(const AssignStmt* a);
    void emitIf(const IfStmt* i);
    void emitWhile(const WhileStmt* w);
    void emitFor(const ForStmt* f);
    void emitReturn(const ReturnStmt* r);

    void emitBlock(const Block& b) {
        pushScope();
        for (auto& s : b.stmts) {
            if (terminated()) break;
            emitStmt(s.get());
        }
        popScope();
    }
    llvm::Value* emitCond(const Expr* e) {
        TV c = emitExpr(e);
        if (!c.val) return llvm::ConstantInt::getFalse(ctx);
        if (c.type->kind != Ty::Bool) {
            err(e->loc, "E0005",
                "condition must be 'bool', found '" + typeName(c.type) + "'");
            return llvm::ConstantInt::getFalse(ctx);
        }
        return c.val;
    }
};

} // namespace jocky
