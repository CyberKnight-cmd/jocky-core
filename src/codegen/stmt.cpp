// Statement emission: variable declarations (incl. destructuring),
// assignment (incl. compound and indexed/field targets), if/while/for,
// and return. (emitBlock/emitCond are inline in internal.h.)
#include "codegen/internal.h"

using namespace llvm;

namespace jocky {

void CodeGen::Impl::emitStmt(const Stmt* s) {
    switch (s->kind) {
        case StmtKind::VarDecl: emitVarDecl((const VarDeclStmt*)s); break;
        case StmtKind::Assign: emitAssign((const AssignStmt*)s); break;
        case StmtKind::ExprStmt: {
            const Expr* e = ((const ExprStmt*)s)->expr.get();
            // A call statement may legitimately produce no value (void).
            if (e->kind == ExprKind::Call) emitCall((const CallExpr*)e);
            else emitExpr(e);
            break;
        }
        case StmtKind::If: emitIf((const IfStmt*)s); break;
        case StmtKind::While: emitWhile((const WhileStmt*)s); break;
        case StmtKind::For: emitFor((const ForStmt*)s); break;
        case StmtKind::Return: emitReturn((const ReturnStmt*)s); break;
        case StmtKind::Block: emitBlock(((const BlockStmt*)s)->block); break;
    }
}

void CodeGen::Impl::emitVarDecl(const VarDeclStmt* v) {
    // Destructuring: `auto a, b = call(...);`
    if (v->names.size() > 1) {
        if (v->declType) {
            err(v->loc, "E0380", "multiple-name declaration must use 'auto'");
            return;
        }
        if (v->init->kind != ExprKind::Call) {
            err(v->loc, "E0381", "multiple-name declaration requires a function call");
            return;
        }
        CallResult cr = emitCall((const CallExpr*)v->init.get());
        if (cr.rets.size() != v->names.size()) {
            err(v->loc, "E0382",
                "function returns " + std::to_string(cr.rets.size()) +
                    " value(s) but " + std::to_string(v->names.size()) + " names given");
            return;
        }
        for (size_t i = 0; i < v->names.size(); i++) {
            Value* elem = (cr.rets.size() == 1) ? cr.raw
                                                : builder.CreateExtractValue(cr.raw, (unsigned)i);
            AllocaInst* a = entryAlloca(lower(cr.rets[i]), v->names[i]);
            builder.CreateStore(elem, a);
            declare(v->names[i], {cr.rets[i], a, v->isConst});
        }
        return;
    }

    const std::string& name = v->names[0];
    // Empty list/map literal with an explicit annotation.
    if (v->declType && v->init) {
        Type* dt = resolveType(v->declType.get());
        if (dt->kind == Ty::List && v->init->kind == ExprKind::ListLit &&
            ((const ListLitExpr*)v->init.get())->elems.empty()) {
            Value* lst = builder.CreateCall(rt("jky_list_new"), {});
            AllocaInst* a = entryAlloca(ptrTy, name);
            builder.CreateStore(lst, a);
            declare(name, {dt, a, v->isConst});
            return;
        }
        if (dt->kind == Ty::Map && v->init->kind == ExprKind::MapLit &&
            ((const MapLitExpr*)v->init.get())->keys.empty()) {
            Value* mp = builder.CreateCall(rt("jky_map_new"), {ci32(mapKeyKind(dt->key))});
            AllocaInst* a = entryAlloca(ptrTy, name);
            builder.CreateStore(mp, a);
            declare(name, {dt, a, v->isConst});
            return;
        }
        TV init = emitExpr(v->init.get());
        Value* val = coerce(init, dt, v->init->loc);
        AllocaInst* a = entryAlloca(lower(dt), name);
        if (val) builder.CreateStore(val, a);
        declare(name, {dt, a, v->isConst});
        return;
    }

    // auto: infer from initializer.
    TV init = emitExpr(v->init.get());
    if (!init.type || init.type->kind == Ty::Error) {
        declare(name, {tc.error_(), nullptr, v->isConst});
        return;
    }
    if (init.type->kind == Ty::Nil) {
        err(v->loc, "E0383", "cannot infer type of '" + name + "' from nil");
        return;
    }
    Type* inferred = concretize(init.type);
    Value* val = coerce(init, inferred, v->init->loc);
    AllocaInst* a = entryAlloca(lower(inferred), name);
    if (val) builder.CreateStore(val, a);
    declare(name, {inferred, a, v->isConst});
}

void CodeGen::Impl::emitAssign(const AssignStmt* a) {
    const Expr* tgt = a->target.get();

    // Compute the new value, given the current value type (for coercion).
    auto computeValue = [&](TV cur, Type* slotType) -> Value* {
        TV rhs = emitExpr(a->value.get());
        if (a->isCompound) {
            TV res = numericOp(cur, rhs, a->compound, a->loc);
            return res.val ? coerce(res, slotType, a->loc) : nullptr;
        }
        return coerce(rhs, slotType, a->value->loc);
    };

    if (tgt->kind == ExprKind::Ident) {
        const std::string& name = ((const IdentExpr*)tgt)->name;
        Sym* s = lookup(name);
        if (!s) { err(tgt->loc, "E0313", "undefined name '" + name + "'"); return; }
        if (s->isConst) { err(a->loc, "E0384", "cannot assign to const '" + name + "'"); return; }
        TV cur{};
        if (a->isCompound) cur = {builder.CreateLoad(lower(s->type), s->addr), s->type};
        Value* val = computeValue(cur, s->type);
        if (val) builder.CreateStore(val, s->addr);
        return;
    }
    if (tgt->kind == ExprKind::Index) {
        auto* ix = (const IndexExpr*)tgt;
        TV obj = emitExpr(ix->object.get());
        if (!obj.val) return;
        if (obj.type->kind == Ty::List) {
            nilCheck(obj.val, ix->loc);
            Value* idx = coerce(emitExpr(ix->index.get()), tc.int_(), ix->index->loc);
            TV cur{};
            if (a->isCompound) {
                Value* slot = builder.CreateCall(rt("jky_list_get"), {obj.val, idx});
                cur = fromSlot(slot, obj.type->elem);
            }
            Value* val = computeValue(cur, obj.type->elem);
            if (val && idx)
                builder.CreateCall(rt("jky_list_set"),
                                   {obj.val, idx, toSlot({val, obj.type->elem})});
            return;
        }
        if (obj.type->kind == Ty::Map) {
            nilCheck(obj.val, ix->loc);
            Value* key = coerce(emitExpr(ix->index.get()), obj.type->key, ix->index->loc);
            TV cur{};
            if (a->isCompound) {
                Value* slot = builder.CreateCall(
                    rt("jky_map_get"),
                    {obj.val, toSlot({key, obj.type->key}), ConstantPointerNull::get(ptrTy)});
                cur = fromSlot(slot, obj.type->elem);
            }
            Value* val = computeValue(cur, obj.type->elem);
            if (val && key)
                builder.CreateCall(rt("jky_map_set"),
                                   {obj.val, toSlot({key, obj.type->key}),
                                    toSlot({val, obj.type->elem})});
            return;
        }
        if (obj.type->kind == Ty::Bytes) {
            nilCheck(obj.val, ix->loc);
            Value* idx = coerce(emitExpr(ix->index.get()), tc.int_(), ix->index->loc);
            TV cur{};
            if (a->isCompound) {
                Value* b = builder.CreateCall(rt("jky_bytes_get"), {obj.val, idx});
                cur = {b, tc.byte_()};
            }
            Value* val = computeValue(cur, tc.byte_());
            if (val && idx)
                builder.CreateCall(rt("jky_bytes_set"), {obj.val, idx, val});
            return;
        }
        err(ix->loc, "E0340", "cannot index-assign a '" + typeName(obj.type) + "'");
        return;
    }
    if (tgt->kind == ExprKind::Member) {
        auto* m = (const MemberExpr*)tgt;
        TV obj = emitExpr(m->object.get());
        if (!obj.val) return;
        if (obj.type->kind != Ty::Struct) {
            err(m->loc, "E0343", "type '" + typeName(obj.type) + "' has no field '" + m->field + "'");
            return;
        }
        StructInfo& si = structs[obj.type->name];
        int idx = -1;
        for (size_t j = 0; j < si.fieldNames.size(); j++)
            if (si.fieldNames[j] == m->field) { idx = (int)j; break; }
        if (idx < 0) { err(m->loc, "E0342", "no field '" + m->field + "'"); return; }
        nilCheck(obj.val, m->loc);
        Value* gep = builder.CreateStructGEP(si.llty, obj.val, (unsigned)idx);
        Type* ft = si.fieldTypes[idx];
        TV cur{};
        if (a->isCompound) cur = {builder.CreateLoad(lower(ft), gep), ft};
        Value* val = computeValue(cur, ft);
        if (val) builder.CreateStore(val, gep);
        return;
    }
    err(a->loc, "E0385", "invalid assignment target");
}

void CodeGen::Impl::emitIf(const IfStmt* i) {
    Value* cond = emitCond(i->cond.get());
    BasicBlock* thenBB = BasicBlock::Create(ctx, "if.then", curLL);
    BasicBlock* elseBB = i->hasElse ? BasicBlock::Create(ctx, "if.else", curLL) : nullptr;
    BasicBlock* endBB = BasicBlock::Create(ctx, "if.end", curLL);
    builder.CreateCondBr(cond, thenBB, i->hasElse ? elseBB : endBB);

    builder.SetInsertPoint(thenBB);
    emitBlock(i->thenBlk);
    if (!terminated()) builder.CreateBr(endBB);

    if (i->hasElse) {
        builder.SetInsertPoint(elseBB);
        if (i->elseIf) emitStmt(i->elseIf.get());
        else emitBlock(i->elseBlk);
        if (!terminated()) builder.CreateBr(endBB);
    }
    builder.SetInsertPoint(endBB);
}

void CodeGen::Impl::emitWhile(const WhileStmt* w) {
    BasicBlock* condBB = BasicBlock::Create(ctx, "while.cond", curLL);
    BasicBlock* bodyBB = BasicBlock::Create(ctx, "while.body", curLL);
    BasicBlock* endBB = BasicBlock::Create(ctx, "while.end", curLL);
    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    Value* cond = emitCond(w->cond.get());
    builder.CreateCondBr(cond, bodyBB, endBB);
    builder.SetInsertPoint(bodyBB);
    emitBlock(w->body);
    if (!terminated()) builder.CreateBr(condBB);
    builder.SetInsertPoint(endBB);
}

void CodeGen::Impl::emitFor(const ForStmt* f) {
    TV iter = emitExpr(f->iterable.get());
    if (!iter.val) return;
    Type* elemTy = nullptr;
    const char* lenFn = nullptr, *getFn = nullptr;
    if (iter.type->kind == Ty::List) { elemTy = iter.type->elem; lenFn = "jky_list_len"; getFn = "jky_list_get"; }
    else if (iter.type->kind == Ty::Bytes) { elemTy = tc.byte_(); lenFn = "jky_bytes_len"; getFn = "jky_bytes_get"; }
    else {
        err(f->loc, "E0390", "cannot iterate over '" + typeName(iter.type) + "'");
        return;
    }
    nilCheck(iter.val, f->loc);
    Value* len = builder.CreateCall(rt(lenFn), {iter.val});
    AllocaInst* idx = entryAlloca(i64, "for.i");
    builder.CreateStore(ci64(0), idx);
    AllocaInst* var = entryAlloca(lower(elemTy), f->var);

    BasicBlock* condBB = BasicBlock::Create(ctx, "for.cond", curLL);
    BasicBlock* bodyBB = BasicBlock::Create(ctx, "for.body", curLL);
    BasicBlock* endBB = BasicBlock::Create(ctx, "for.end", curLL);
    builder.CreateBr(condBB);
    builder.SetInsertPoint(condBB);
    Value* iv = builder.CreateLoad(i64, idx);
    builder.CreateCondBr(builder.CreateICmpSLT(iv, len), bodyBB, endBB);

    builder.SetInsertPoint(bodyBB);
    Value* iv2 = builder.CreateLoad(i64, idx);
    Value* raw = builder.CreateCall(rt(getFn), {iter.val, iv2});
    Value* elem;
    if (iter.type->kind == Ty::Bytes) elem = raw; // already i8
    else elem = fromSlot(raw, elemTy).val;
    builder.CreateStore(elem, var);

    pushScope();
    declare(f->var, {elemTy, var, false});
    emitBlock(f->body);
    popScope();
    if (!terminated()) {
        Value* next = builder.CreateAdd(builder.CreateLoad(i64, idx), ci64(1));
        builder.CreateStore(next, idx);
        builder.CreateBr(condBB);
    }
    builder.SetInsertPoint(endBB);
}

void CodeGen::Impl::emitReturn(const ReturnStmt* r) {
    const std::vector<Type*>& rets = curFn->returns;
    bool isVoid = rets.empty() || (rets.size() == 1 && rets[0]->kind == Ty::Void);

    if (isVoid) {
        if (!r->values.empty())
            err(r->loc, "E0391", "void function cannot return a value");
        builder.CreateRet(ci64(0)); // ABI: void maps to i64 0
        return;
    }
    if (r->values.size() != rets.size()) {
        err(r->loc, "E0392",
            "function returns " + std::to_string(rets.size()) + " value(s), got " +
                std::to_string(r->values.size()));
        builder.CreateRet(Constant::getNullValue(fnRetType(rets)));
        return;
    }
    if (rets.size() == 1) {
        Value* v = coerce(emitExpr(r->values[0].get()), rets[0], r->values[0]->loc);
        builder.CreateRet(v ? v : Constant::getNullValue(lower(rets[0])));
        return;
    }
    Value* agg = UndefValue::get(fnRetType(rets));
    for (size_t i = 0; i < rets.size(); i++) {
        Value* v = coerce(emitExpr(r->values[i].get()), rets[i], r->values[i]->loc);
        if (!v) v = Constant::getNullValue(lower(rets[i]));
        agg = builder.CreateInsertValue(agg, v, (unsigned)i);
    }
    builder.CreateRet(agg);
}


} // namespace jocky
