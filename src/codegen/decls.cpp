// Declaration passes: registering structs and function signatures,
// top-level consts (globals), and emitting function skeletons (entry block,
// parameter allocas, fall-through return).
#include "codegen/internal.h"

using namespace llvm;

namespace jocky {

bool CodeGen::Impl::declareStructsAndFns() {
for (auto& m : prog.imports) modules.insert(m.module);

    // Register struct names first (so fields can reference each other).
    for (auto& s : prog.structs) {
        if (structs.count(s->name)) {
            err(s->loc, "E0301", "duplicate struct '" + s->name + "'");
            continue;
        }
        StructInfo si;
        si.decl = s.get();
        si.llty = StructType::create(ctx, "struct." + s->name);
        structs[s->name] = si;
        tc.struct_(s->name, s.get());
    }
    // Predeclared Error struct { int code; string message }.
    if (!structs.count("Error")) {
        StructInfo si;
        si.decl = nullptr;
        si.llty = StructType::create(ctx, "struct.Error");
        si.fieldNames = {"code", "message"};
        si.fieldTypes = {tc.int_(), tc.string_()};
        si.llty->setBody({i64, ptrTy});
        structs["Error"] = si;
        tc.struct_("Error", nullptr);
    }
    // Resolve struct field types + set LLVM bodies.
    for (auto& s : prog.structs) {
        StructInfo& si = structs[s->name];
        std::vector<llvm::Type*> body;
        for (auto& f : s->fields) {
            Type* ft = resolveType(f.type.get());
            si.fieldTypes.push_back(ft);
            si.fieldNames.push_back(f.name);
            body.push_back(lower(ft));
        }
        si.llty->setBody(body);
    }

    // Functions
    for (auto& fn : prog.functions) {
        if (funcs.count(fn->name)) {
            err(fn->loc, "E0302", "duplicate function '" + fn->name + "'");
            continue;
        }
        FnInfo fi;
        for (auto& p : fn->params) fi.params.push_back(resolveType(p.type.get()));
        for (auto& r : fn->returns) fi.returns.push_back(resolveType(r.get()));
        std::vector<llvm::Type*> paramLL;
        for (auto* p : fi.params) paramLL.push_back(lower(p));
        // JOCKY `main` maps to runtime symbol `jky_main`.
        std::string sym = (fn->name == "main") ? "jky_main" : fn->name;
        FunctionType* ft = FunctionType::get(fnRetType(fi.returns), paramLL, false);
        fi.fn = Function::Create(ft, Function::ExternalLinkage, sym, mod.get());
        funcs[fn->name] = fi;
    }
    if (!funcs.count("main"))
        err({0, 0}, "E0303", "program has no 'fn main'");
    return ok();
}

void CodeGen::Impl::emitFunction(const FnDecl* fn) {
    FnInfo& fi = funcs[fn->name];
    curFn = &fi;
    curLL = fi.fn;
    BasicBlock* entry = BasicBlock::Create(ctx, "entry", fi.fn);
    builder.SetInsertPoint(entry);
    pushScope();

    // If main, run global initializers first.
    if (fn->name == "main" && !globalDecls.empty())
        builder.CreateCall(mod->getOrInsertFunction(
            "jky_init_globals", FunctionType::get(voidTy, false)));

    unsigned ai = 0;
    for (auto& arg : fi.fn->args()) {
        const Param& p = fn->params[ai];
        arg.setName(p.name);
        AllocaInst* slot = entryAlloca(lower(fi.params[ai]), p.name);
        builder.CreateStore(&arg, slot);
        declare(p.name, {fi.params[ai], slot, false});
        ai++;
    }

    emitBlock(fn->body);
    if (!terminated()) {
        // Fall off the end: void/main -> 0; typed -> null (checker should catch
        // missing returns, but keep IR well-formed).
        builder.CreateRet(Constant::getNullValue(fnRetType(fi.returns)));
    }
    popScope();
    curFn = nullptr;
    curLL = nullptr;
}

// ---- top-level consts (globals) -------------------------------------------
void CodeGen::Impl::declareGlobals() {
    for (auto& g : prog.globals) {
        const VarDeclStmt* v = g.get();
        if (v->names.size() != 1) {
            err(v->loc, "E0393", "global declaration must have a single name");
            continue;
        }
        globalDecls.push_back(v);
    }
    if (!globalDecls.empty()) emitGlobalInit();
}

void CodeGen::Impl::emitGlobalInit() {
    // Create globals with a type we resolve from the declared type or a best
    // guess; initialize them in jky_init_globals().
    Function* init = Function::Create(
        FunctionType::get(voidTy, false), Function::ExternalLinkage,
        "jky_init_globals", mod.get());
    BasicBlock* bb = BasicBlock::Create(ctx, "entry", init);
    curLL = init;
    builder.SetInsertPoint(bb);
    for (const VarDeclStmt* v : globalDecls) {
        Type* dt = v->declType ? resolveType(v->declType.get()) : nullptr;
        TV init0 = emitExpr(v->init.get());
        Type* ty = dt ? dt : concretize(init0.type);
        Value* val = coerce(init0, ty, v->init->loc);
        auto* gv = new GlobalVariable(*mod, lower(ty), false,
                                      GlobalValue::InternalLinkage,
                                      Constant::getNullValue(lower(ty)), v->names[0]);
        if (val) builder.CreateStore(val, gv);
        // Register in the persistent global scope.
        scopes.back()[v->names[0]] = {ty, gv, v->isConst};
    }
    builder.CreateRetVoid();
    curLL = nullptr;
}

} // namespace jocky
