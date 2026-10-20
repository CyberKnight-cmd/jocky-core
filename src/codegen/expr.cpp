// Expression and call emission: literals, operators, casts, indexing,
// member access, list/map/struct literals, interpolation, and the three
// call forms (user functions, module builtins, value methods).
#include "codegen/internal.h"

using namespace llvm;

namespace jocky {

TV CodeGen::Impl::emitExpr(const Expr* e) {
    switch (e->kind) {
        case ExprKind::IntLit:
            return {ci64(((const IntLitExpr*)e)->value), tc.untypedInt()};
        case ExprKind::FloatLit:
            return {ConstantFP::get(dbl, ((const FloatLitExpr*)e)->value), tc.untypedFloat()};
        case ExprKind::CharLit:
            return {ConstantInt::get(i8, (uint64_t)((const CharLitExpr*)e)->value), tc.byte_()};
        case ExprKind::BoolLit:
            return {((const BoolLitExpr*)e)->value ? ConstantInt::getTrue(ctx)
                                                   : ConstantInt::getFalse(ctx),
                    tc.bool_()};
        case ExprKind::NilLit:
            return {ConstantPointerNull::get(ptrTy), tc.nil_()};
        case ExprKind::StringLit: {
            auto* x = (const StringLitExpr*)e;
            Value* p = globalCStr(x->value);
            Value* s = builder.CreateCall(rt("jky_str_new"),
                                          {p, ci64((int64_t)x->value.size())});
            return {s, tc.string_()};
        }
        case ExprKind::HexBytesLit: {
            auto* x = (const HexBytesLitExpr*)e;
            Constant* arr = ConstantDataArray::get(ctx, ArrayRef<uint8_t>(x->bytes));
            auto* gv = new GlobalVariable(*mod, arr->getType(), true,
                                          GlobalValue::PrivateLinkage, arr, "hexlit");
            Value* s = builder.CreateCall(rt("jky_bytes_new"),
                                          {gv, ci64((int64_t)x->bytes.size())});
            return {s, tc.bytes_()};
        }
        case ExprKind::InterpString: return emitInterp((const InterpStringExpr*)e);
        case ExprKind::ListLit: return emitListLit((const ListLitExpr*)e);
        case ExprKind::MapLit: return emitMapLit((const MapLitExpr*)e);
        case ExprKind::StructLit: return emitStructLit((const StructLitExpr*)e);
        case ExprKind::Ident: {
            auto* x = (const IdentExpr*)e;
            if (Sym* s = lookup(x->name)) {
                Value* v = builder.CreateLoad(lower(s->type), s->addr, x->name);
                return {v, s->type};
            }
            if (funcs.count(x->name) || modules.count(x->name))
                err(e->loc, "E0312", "'" + x->name + "' is not a value");
            else
                err(e->loc, "E0313", "undefined name '" + x->name + "'");
            return {nullptr, tc.error_()};
        }
        case ExprKind::Unary: return emitUnary((const UnaryExpr*)e);
        case ExprKind::Binary: return emitBinary((const BinaryExpr*)e);
        case ExprKind::Cast: return emitCast((const CastExpr*)e);
        case ExprKind::Member: return emitMember((const MemberExpr*)e);
        case ExprKind::Index: return emitIndex((const IndexExpr*)e);
        case ExprKind::Call: {
            CallResult cr = emitCall((const CallExpr*)e);
            if (cr.rets.empty()) {
                err(e->loc, "E0314", "call produces no value here");
                return {nullptr, tc.error_()};
            }
            if (cr.rets.size() > 1) {
                err(e->loc, "E0315",
                    "call returns " + std::to_string(cr.rets.size()) +
                        " values; expected 1");
                return {nullptr, tc.error_()};
            }
            return {cr.raw, cr.rets[0]};
        }
    }
    return {nullptr, tc.error_()};
}

TV CodeGen::Impl::emitInterp(const InterpStringExpr* s) {
    auto mkLit = [&](const std::string& lit) -> Value* {
        Value* p = globalCStr(lit);
        return builder.CreateCall(rt("jky_str_new"), {p, ci64((int64_t)lit.size())});
    };
    Value* acc = mkLit(s->literals[0]);
    for (size_t i = 0; i < s->exprs.size(); i++) {
        TV t = emitExpr(s->exprs[i].get());
        if (t.val) {
            Value* str = toStringValue(t, s->exprs[i]->loc);
            acc = builder.CreateCall(rt("jky_str_concat"), {acc, str});
        }
        acc = builder.CreateCall(rt("jky_str_concat"), {acc, mkLit(s->literals[i + 1])});
    }
    return {acc, tc.string_()};
}

TV CodeGen::Impl::emitListLit(const ListLitExpr* a) {
    if (a->elems.empty()) {
        err(a->loc, "E0320",
            "cannot infer element type of empty list literal; annotate the variable");
        return {nullptr, tc.error_()};
    }
    TV first = emitExpr(a->elems[0].get());
    Type* et = concretize(first.type);
    Value* list = builder.CreateCall(rt("jky_list_new"), {});
    // append first
    Value* fv = coerce(first, et, a->elems[0]->loc);
    if (fv) builder.CreateCall(rt("jky_list_append"), {list, toSlot({fv, et})});
    for (size_t i = 1; i < a->elems.size(); i++) {
        TV t = emitExpr(a->elems[i].get());
        Value* v = coerce(t, et, a->elems[i]->loc);
        if (v) builder.CreateCall(rt("jky_list_append"), {list, toSlot({v, et})});
    }
    return {list, tc.list(et)};
}

TV CodeGen::Impl::emitMapLit(const MapLitExpr* m) {
    if (m->keys.empty()) {
        err(m->loc, "E0321",
            "cannot infer key/value types of empty map literal; annotate the variable");
        return {nullptr, tc.error_()};
    }
    TV k0 = emitExpr(m->keys[0].get());
    TV v0 = emitExpr(m->values[0].get());
    Type* kt = concretize(k0.type);
    Type* vt = concretize(v0.type);
    Value* map = builder.CreateCall(rt("jky_map_new"), {ci32(mapKeyKind(kt))});
    auto setPair = [&](TV kk, TV vv, Loc l) {
        Value* kc = coerce(kk, kt, l);
        Value* vc = coerce(vv, vt, l);
        if (kc && vc)
            builder.CreateCall(rt("jky_map_set"),
                               {map, toSlot({kc, kt}), toSlot({vc, vt})});
    };
    setPair(k0, v0, m->loc);
    for (size_t i = 1; i < m->keys.size(); i++)
        setPair(emitExpr(m->keys[i].get()), emitExpr(m->values[i].get()), m->loc);
    return {map, tc.map(kt, vt)};
}

TV CodeGen::Impl::emitStructLit(const StructLitExpr* s) {
    auto it = structs.find(s->typeName);
    if (it == structs.end()) {
        err(s->loc, "E0322", "unknown struct '" + s->typeName + "'");
        return {nullptr, tc.error_()};
    }
    StructInfo& si = it->second;
    uint64_t size = mod->getDataLayout().getTypeAllocSize(si.llty);
    Value* obj = builder.CreateCall(rt("jky_alloc"), {ci64((int64_t)size)});
    for (size_t i = 0; i < s->fieldNames.size(); i++) {
        int idx = -1;
        for (size_t j = 0; j < si.fieldNames.size(); j++)
            if (si.fieldNames[j] == s->fieldNames[i]) { idx = (int)j; break; }
        if (idx < 0) {
            err(s->loc, "E0323",
                "struct '" + s->typeName + "' has no field '" + s->fieldNames[i] + "'");
            continue;
        }
        TV fv = emitExpr(s->fieldValues[i].get());
        Value* v = coerce(fv, si.fieldTypes[idx], s->fieldValues[i]->loc);
        if (!v) continue;
        Value* gep = builder.CreateStructGEP(si.llty, obj, (unsigned)idx);
        builder.CreateStore(v, gep);
    }
    return {obj, tc.struct_(s->typeName, si.decl)};
}

TV CodeGen::Impl::emitUnary(const UnaryExpr* u) {
    // Fold negation of an untyped literal so it stays untyped (D4 range checks).
    if (u->op == UnOp::Neg && u->operand->kind == ExprKind::IntLit) {
        auto* lit = (const IntLitExpr*)u->operand.get();
        return {ci64(-lit->value), tc.untypedInt()};
    }
    if (u->op == UnOp::Neg && u->operand->kind == ExprKind::FloatLit) {
        auto* lit = (const FloatLitExpr*)u->operand.get();
        return {ConstantFP::get(dbl, -lit->value), tc.untypedFloat()};
    }
    TV v = emitExpr(u->operand.get());
    if (!v.val) return v;
    switch (u->op) {
        case UnOp::Neg:
            if (v.type->kind == Ty::Int) return {builder.CreateNeg(v.val), tc.int_()};
            if (v.type->kind == Ty::Float) return {builder.CreateFNeg(v.val), tc.float_()};
            if (v.type->kind == Ty::Byte) return {builder.CreateNeg(v.val), tc.byte_()};
            err(u->loc, "E0330", "unary '-' requires a numeric type");
            return {nullptr, tc.error_()};
        case UnOp::Not:
            if (v.type->kind == Ty::Bool)
                return {builder.CreateXor(v.val, ConstantInt::getTrue(ctx)), tc.bool_()};
            err(u->loc, "E0331", "unary '!' requires 'bool'");
            return {nullptr, tc.error_()};
        case UnOp::BitNot:
            if (v.type->kind == Ty::Int)
                return {builder.CreateXor(v.val, ci64(-1)), tc.int_()};
            if (v.type->kind == Ty::Byte)
                return {builder.CreateXor(v.val, ConstantInt::get(i8, 0xFF)), tc.byte_()};
            err(u->loc, "E0332", "unary '~' requires 'int' or 'byte'");
            return {nullptr, tc.error_()};
    }
    return {nullptr, tc.error_()};
}

// Unify two numeric operands, applying untyped-literal coercion (D4).
// Returns common type or nullptr (error already reported).
static bool isNumericKind(Ty k) {
    return k == Ty::Int || k == Ty::Float || k == Ty::Byte ||
           k == Ty::UntypedInt || k == Ty::UntypedFloat;
}

TV CodeGen::Impl::emitBinary(const BinaryExpr* b) {
    // Short-circuit logical operators.
    if (b->op == BinOp::And || b->op == BinOp::Or) {
        Value* l = emitCond(b->lhs.get());
        BasicBlock* entry = builder.GetInsertBlock();
        BasicBlock* rhsBB = BasicBlock::Create(ctx, "sc.rhs", curLL);
        BasicBlock* endBB = BasicBlock::Create(ctx, "sc.end", curLL);
        if (b->op == BinOp::And) builder.CreateCondBr(l, rhsBB, endBB);
        else builder.CreateCondBr(l, endBB, rhsBB);
        builder.SetInsertPoint(rhsBB);
        Value* r = emitCond(b->rhs.get());
        BasicBlock* rhsEnd = builder.GetInsertBlock();
        builder.CreateBr(endBB);
        builder.SetInsertPoint(endBB);
        PHINode* phi = builder.CreatePHI(i1, 2);
        Value* shortVal = (b->op == BinOp::And) ? (Value*)ConstantInt::getFalse(ctx)
                                                : (Value*)ConstantInt::getTrue(ctx);
        phi->addIncoming(shortVal, entry);
        phi->addIncoming(r, rhsEnd);
        return {phi, tc.bool_()};
    }

    TV L = emitExpr(b->lhs.get());
    TV R = emitExpr(b->rhs.get());
    if (!L.val || !R.val) return {nullptr, tc.error_()};
    Loc l = b->loc;

    // Equality / inequality
    if (b->op == BinOp::Eq || b->op == BinOp::Ne) {
        bool ne = (b->op == BinOp::Ne);
        // nil comparisons
        if (L.type->kind == Ty::Nil || R.type->kind == Ty::Nil) {
            Type* other = (L.type->kind == Ty::Nil) ? R.type : L.type;
            if (other->kind != Ty::Nil && !other->isReference()) {
                err(l, "E0007", "cannot compare '" + typeName(other) + "' with nil");
                return {nullptr, tc.error_()};
            }
            Value* lp = L.val, *rp = R.val;
            Value* cmp = ne ? builder.CreateICmpNE(lp, rp) : builder.CreateICmpEQ(lp, rp);
            return {cmp, tc.bool_()};
        }
        if (L.type->kind == Ty::String && R.type->kind == Ty::String) {
            Value* eq = builder.CreateCall(rt("jky_str_eq"), {L.val, R.val});
            Value* b1 = builder.CreateTrunc(eq, i1);
            return {ne ? builder.CreateXor(b1, ConstantInt::getTrue(ctx)) : b1, tc.bool_()};
        }
        if (L.type->kind == Ty::Bytes && R.type->kind == Ty::Bytes) {
            Value* eq = builder.CreateCall(rt("jky_bytes_eq"), {L.val, R.val});
            Value* b1 = builder.CreateTrunc(eq, i1);
            return {ne ? builder.CreateXor(b1, ConstantInt::getTrue(ctx)) : b1, tc.bool_()};
        }
        if (L.type->kind == Ty::Bool && R.type->kind == Ty::Bool) {
            Value* c = ne ? builder.CreateICmpNE(L.val, R.val)
                          : builder.CreateICmpEQ(L.val, R.val);
            return {c, tc.bool_()};
        }
        // Numeric ==/!= falls through to the unify+switch below, which emits
        // the icmp/fcmp. Anything else is a type error.
        if (!(isNumericKind(L.type->kind) && isNumericKind(R.type->kind))) {
            err(l, "E0007", "cannot compare '" + typeName(L.type) + "' with '" +
                                typeName(R.type) + "'");
            return {nullptr, tc.error_()};
        }
    }

    // Numeric unify for arithmetic/relational/bitwise and numeric (in)equality.
    // String concatenation with '+'.
    if (b->op == BinOp::Add && (L.type->kind == Ty::String || R.type->kind == Ty::String)) {
        if (L.type->kind == Ty::String && R.type->kind == Ty::String)
            return {builder.CreateCall(rt("jky_str_concat"), {L.val, R.val}), tc.string_()};
        err(l, "E0007", "operator '+' needs both operands to be 'string'");
        return {nullptr, tc.error_()};
    }

    return numericOp(L, R, b->op, l);
}

TV CodeGen::Impl::numericOp(TV L, TV R, BinOp op, Loc l) {
    if (!isNumericKind(L.type->kind) || !isNumericKind(R.type->kind)) {
        err(l, "E0007", std::string("operator '") + binOpName(op) +
                            "' cannot be applied to '" + typeName(L.type) + "' and '" +
                            typeName(R.type) + "'");
        return {nullptr, tc.error_()};
    }

    // Determine common numeric type.
    Type* common = nullptr;
    bool lu = L.type->isUntyped(), ru = R.type->isUntyped();
    if (lu && ru)
        common = (L.type->kind == Ty::UntypedFloat || R.type->kind == Ty::UntypedFloat)
                     ? tc.float_() : tc.int_();
    else if (lu) common = R.type;
    else if (ru) common = L.type;
    else {
        if (!sameType(L.type, R.type)) {
            err(l, "E0007", std::string("operator '") + binOpName(op) +
                                "' cannot be applied to '" + typeName(L.type) + "' and '" +
                                typeName(R.type) + "'");
            return {nullptr, tc.error_()};
        }
        common = L.type;
    }
    Value* lv = coerce(L, common, l);
    Value* rv = coerce(R, common, l);
    if (!lv || !rv) return {nullptr, tc.error_()};

    bool isFloat = common->kind == Ty::Float;
    bool isByte = common->kind == Ty::Byte; // unsigned

    switch (op) {
        case BinOp::Add: return {isFloat ? builder.CreateFAdd(lv, rv) : builder.CreateAdd(lv, rv), common};
        case BinOp::Sub: return {isFloat ? builder.CreateFSub(lv, rv) : builder.CreateSub(lv, rv), common};
        case BinOp::Mul: return {isFloat ? builder.CreateFMul(lv, rv) : builder.CreateMul(lv, rv), common};
        case BinOp::Div:
            return {isFloat ? builder.CreateFDiv(lv, rv)
                            : (isByte ? builder.CreateUDiv(lv, rv) : builder.CreateSDiv(lv, rv)),
                    common};
        case BinOp::Mod:
            if (isFloat) { err(l, "E0007", "'%' not defined for 'float'"); return {nullptr, tc.error_()}; }
            return {isByte ? builder.CreateURem(lv, rv) : builder.CreateSRem(lv, rv), common};
        case BinOp::Eq:
            return {isFloat ? builder.CreateFCmpOEQ(lv, rv) : builder.CreateICmpEQ(lv, rv), tc.bool_()};
        case BinOp::Ne:
            return {isFloat ? builder.CreateFCmpONE(lv, rv) : builder.CreateICmpNE(lv, rv), tc.bool_()};
        case BinOp::Lt:
            return {isFloat ? builder.CreateFCmpOLT(lv, rv)
                            : (isByte ? builder.CreateICmpULT(lv, rv) : builder.CreateICmpSLT(lv, rv)),
                    tc.bool_()};
        case BinOp::Le:
            return {isFloat ? builder.CreateFCmpOLE(lv, rv)
                            : (isByte ? builder.CreateICmpULE(lv, rv) : builder.CreateICmpSLE(lv, rv)),
                    tc.bool_()};
        case BinOp::Gt:
            return {isFloat ? builder.CreateFCmpOGT(lv, rv)
                            : (isByte ? builder.CreateICmpUGT(lv, rv) : builder.CreateICmpSGT(lv, rv)),
                    tc.bool_()};
        case BinOp::Ge:
            return {isFloat ? builder.CreateFCmpOGE(lv, rv)
                            : (isByte ? builder.CreateICmpUGE(lv, rv) : builder.CreateICmpSGE(lv, rv)),
                    tc.bool_()};
        case BinOp::BitAnd: case BinOp::BitOr: case BinOp::BitXor:
        case BinOp::Shl: case BinOp::Shr:
            if (isFloat) { err(l, "E0007", "bitwise op not defined for 'float'"); return {nullptr, tc.error_()}; }
            switch (op) {
                case BinOp::BitAnd: return {builder.CreateAnd(lv, rv), common};
                case BinOp::BitOr:  return {builder.CreateOr(lv, rv), common};
                case BinOp::BitXor: return {builder.CreateXor(lv, rv), common};
                case BinOp::Shl:    return {builder.CreateShl(lv, rv), common};
                case BinOp::Shr:    return {isByte ? builder.CreateLShr(lv, rv)
                                                   : builder.CreateAShr(lv, rv), common};
                default: break;
            }
            break;
        default: break;
    }
    return {nullptr, tc.error_()};
}

TV CodeGen::Impl::emitCast(const CastExpr* c) {
    Type* tgt = resolveType(c->target.get());
    TV v = emitExpr(c->operand.get());
    if (!v.val) return {nullptr, tc.error_()};
    Ty from = v.type->kind;
    if (from == Ty::UntypedInt) from = Ty::Int;
    if (from == Ty::UntypedFloat) from = Ty::Float;
    Ty to = tgt->kind;

    auto bad = [&]() -> TV {
        err(c->loc, "E0311", "cannot cast '" + typeName(v.type) + "' to '" + typeName(tgt) + "'");
        return {nullptr, tc.error_()};
    };

    switch (to) {
        case Ty::Int:
            if (from == Ty::Int) return {v.val, tgt};
            if (from == Ty::Float) return {builder.CreateFPToSI(v.val, i64), tgt};
            if (from == Ty::Byte) return {builder.CreateZExt(v.val, i64), tgt};
            return bad();
        case Ty::Float:
            if (from == Ty::Float) return {v.val, tgt};
            if (from == Ty::Int) return {builder.CreateSIToFP(v.val, dbl), tgt};
            if (from == Ty::Byte)
                return {builder.CreateSIToFP(builder.CreateZExt(v.val, i64), dbl), tgt};
            return bad();
        case Ty::Byte:
            if (from == Ty::Byte) return {v.val, tgt};
            if (from == Ty::Int) return {builder.CreateTrunc(v.val, i8), tgt};
            if (from == Ty::Float) return {builder.CreateTrunc(builder.CreateFPToSI(v.val, i64), i8), tgt};
            return bad();
        case Ty::String:
            if (from == Ty::String) return {v.val, tgt};
            if (from == Ty::Int || from == Ty::Float || from == Ty::Bool || from == Ty::Byte) {
                TV tmp{v.val, (from == Ty::Int) ? tc.int_()
                              : (from == Ty::Float) ? tc.float_()
                              : (from == Ty::Bool) ? tc.bool_() : tc.byte_()};
                return {toStringValue(tmp, c->loc), tgt};
            }
            return bad();
        default:
            if (sameType(v.type, tgt)) return {v.val, tgt};
            return bad();
    }
}

TV CodeGen::Impl::emitIndex(const IndexExpr* ix) {
    TV obj = emitExpr(ix->object.get());
    if (!obj.val) return {nullptr, tc.error_()};
    if (obj.type->kind == Ty::List) {
        nilCheck(obj.val, ix->loc);
        TV idx = emitExpr(ix->index.get());
        Value* iv = coerce(idx, tc.int_(), ix->index->loc);
        if (!iv) return {nullptr, tc.error_()};
        Value* slot = builder.CreateCall(rt("jky_list_get"), {obj.val, iv});
        return fromSlot(slot, obj.type->elem);
    }
    if (obj.type->kind == Ty::Bytes) {
        nilCheck(obj.val, ix->loc);
        TV idx = emitExpr(ix->index.get());
        Value* iv = coerce(idx, tc.int_(), ix->index->loc);
        if (!iv) return {nullptr, tc.error_()};
        Value* byte = builder.CreateCall(rt("jky_bytes_get"), {obj.val, iv});
        return {byte, tc.byte_()};
    }
    if (obj.type->kind == Ty::Map) {
        nilCheck(obj.val, ix->loc);
        TV key = emitExpr(ix->index.get());
        Value* kv = coerce(key, obj.type->key, ix->index->loc);
        if (!kv) return {nullptr, tc.error_()};
        Value* slot = builder.CreateCall(
            rt("jky_map_get"),
            {obj.val, toSlot({kv, obj.type->key}), ConstantPointerNull::get(ptrTy)});
        return fromSlot(slot, obj.type->elem);
    }
    err(ix->loc, "E0340", "cannot index a value of type '" + typeName(obj.type) + "'");
    return {nullptr, tc.error_()};
}

TV CodeGen::Impl::emitMember(const MemberExpr* m) {
    // `module.x` as a value is not allowed.
    if (m->object->kind == ExprKind::Ident) {
        const std::string& on = ((const IdentExpr*)m->object.get())->name;
        if (modules.count(on) && !lookup(on)) {
            err(m->loc, "E0341", "'" + on + "' is a module, not a value");
            return {nullptr, tc.error_()};
        }
    }
    TV obj = emitExpr(m->object.get());
    if (!obj.val) return {nullptr, tc.error_()};
    if (obj.type->kind == Ty::Struct) {
        StructInfo& si = structs[obj.type->name];
        int idx = -1;
        for (size_t j = 0; j < si.fieldNames.size(); j++)
            if (si.fieldNames[j] == m->field) { idx = (int)j; break; }
        if (idx < 0) {
            err(m->loc, "E0342",
                "type '" + obj.type->name + "' has no field '" + m->field + "'");
            return {nullptr, tc.error_()};
        }
        nilCheck(obj.val, m->loc);
        Value* gep = builder.CreateStructGEP(si.llty, obj.val, (unsigned)idx);
        Type* ft = si.fieldTypes[idx];
        return {builder.CreateLoad(lower(ft), gep), ft};
    }
    err(m->loc, "E0343",
        "type '" + typeName(obj.type) + "' has no field '" + m->field + "'");
    return {nullptr, tc.error_()};
}

// ---- calls ----------------------------------------------------------------
CodeGen::Impl::CallResult CodeGen::Impl::emitCall(const CallExpr* c) {
    const Expr* callee = c->callee.get();

    // Direct function call, or JKY_ERR builtin.
    if (callee->kind == ExprKind::Ident) {
        const std::string& name = ((const IdentExpr*)callee)->name;
        if (name == "JKY_ERR") {
            if (c->args.size() != 1) {
                err(c->loc, "E0350", "JKY_ERR expects 1 argument");
                return {};
            }
            TV msg = emitExpr(c->args[0].get());
            Value* mv = coerce(msg, tc.string_(), c->args[0]->loc);
            Value* e = builder.CreateCall(rt("jky_err_new"),
                                          {ci64(0), mv ? mv : ConstantPointerNull::get(ptrTy)});
            return {e, {tc.struct_("Error", structs["Error"].decl)}};
        }
        auto it = funcs.find(name);
        if (it == funcs.end()) {
            err(c->loc, "E0351", "call to undefined function '" + name + "'");
            return {};
        }
        FnInfo& fi = it->second;
        if (c->args.size() != fi.params.size()) {
            err(c->loc, "E0352",
                "function '" + name + "' expects " + std::to_string(fi.params.size()) +
                    " argument(s), got " + std::to_string(c->args.size()));
            return {};
        }
        std::vector<Value*> args;
        for (size_t i = 0; i < c->args.size(); i++) {
            TV a = emitExpr(c->args[i].get());
            Value* v = coerce(a, fi.params[i], c->args[i]->loc);
            args.push_back(v ? v : ConstantPointerNull::get(ptrTy));
        }
        Value* call = builder.CreateCall(fi.fn, args);
        std::vector<Type*> rets = fi.returns;
        if (rets.size() == 1 && rets[0]->kind == Ty::Void) rets.clear();
        return {call, rets};
    }

    if (callee->kind == ExprKind::Member) {
        auto* m = (const MemberExpr*)callee;
        // module call
        if (m->object->kind == ExprKind::Ident) {
            const std::string& on = ((const IdentExpr*)m->object.get())->name;
            if (modules.count(on) && !lookup(on))
                return emitModuleCall(on, m->field, c);
        }
        // method call on a value
        TV recv = emitExpr(m->object.get());
        if (!recv.val) return {};
        return emitMethodCall(recv, m->field, c);
    }

    err(c->loc, "E0353", "expression is not callable");
    return {};
}

// ---- module builtins & value methods ----
CodeGen::Impl::CallResult
CodeGen::Impl::emitModuleCall(const std::string& modName, const std::string& fn,
                              const CallExpr* c) {
    auto arg1str = [&](const char* rtfn) -> CallResult {
        if (c->args.size() != 1) {
            err(c->loc, "E0360", modName + "." + fn + " expects 1 argument");
            return {};
        }
        TV a = emitExpr(c->args[0].get());
        Value* s = coerce(a, tc.string_(), c->args[0]->loc);
        builder.CreateCall(rt(rtfn), {s ? s : ConstantPointerNull::get(ptrTy)});
        return {nullptr, {}};
    };
    if (modName == "log") {
        if (fn == "info") return arg1str("jky_log_info");
        if (fn == "warn") return arg1str("jky_log_warn");
        if (fn == "error") return arg1str("jky_log_error");
        err(c->loc, "E0361", "module 'log' has no function '" + fn + "'");
        return {};
    }
    if (modName == "fs") {
        if (fn == "read") {
            if (c->args.size() != 1) {
                err(c->loc, "E0360", "fs.read expects 1 argument");
                return {};
            }
            TV a = emitExpr(c->args[0].get());
            Value* path = coerce(a, tc.string_(), c->args[0]->loc);
            Value* r = builder.CreateCall(rt("jky_fs_read"),
                                          {path ? path : ConstantPointerNull::get(ptrTy)});
            return {r, {tc.bytes_(), tc.struct_("Error", structs["Error"].decl)}};
        }
        err(c->loc, "E0361", "module 'fs' has no function '" + fn + "'");
        return {};
    }
    err(c->loc, "E0362", "unknown module '" + modName + "'");
    return {};
}

// ---- method calls ---------------------------------------------------------
CodeGen::Impl::CallResult
CodeGen::Impl::emitMethodCall(TV recv, const std::string& method, const CallExpr* c) {
    if (recv.type->isReference()) nilCheck(recv.val, c->loc);
    auto wrongArgs = [&](int n) {
        err(c->loc, "E0370",
            "method '" + method + "' expects " + std::to_string(n) + " argument(s)");
    };
    auto argInt = [&](size_t i) -> Value* {
        TV a = emitExpr(c->args[i].get());
        return coerce(a, tc.int_(), c->args[i]->loc);
    };

    switch (recv.type->kind) {
        case Ty::String:
            if (method == "len") {
                if (!c->args.empty()) { wrongArgs(0); return {}; }
                return {builder.CreateCall(rt("jky_str_len"), {recv.val}), {tc.int_()}};
            }
            if (method == "contains") {
                if (c->args.size() != 1) { wrongArgs(1); return {}; }
                TV a = emitExpr(c->args[0].get());
                Value* sub = coerce(a, tc.string_(), c->args[0]->loc);
                Value* r = builder.CreateCall(rt("jky_str_contains"), {recv.val, sub});
                return {builder.CreateTrunc(r, i1), {tc.bool_()}};
            }
            if (method == "slice") {
                if (c->args.size() != 2) { wrongArgs(2); return {}; }
                Value* a0 = argInt(0), *a1 = argInt(1);
                return {builder.CreateCall(rt("jky_str_slice"), {recv.val, a0, a1}), {tc.string_()}};
            }
            break;
        case Ty::Bytes:
            if (method == "len") {
                if (!c->args.empty()) { wrongArgs(0); return {}; }
                return {builder.CreateCall(rt("jky_bytes_len"), {recv.val}), {tc.int_()}};
            }
            if (method == "slice") {
                if (c->args.size() != 2) { wrongArgs(2); return {}; }
                Value* a0 = argInt(0), *a1 = argInt(1);
                return {builder.CreateCall(rt("jky_bytes_slice"), {recv.val, a0, a1}), {tc.bytes_()}};
            }
            break;
        case Ty::List:
            if (method == "len") {
                if (!c->args.empty()) { wrongArgs(0); return {}; }
                return {builder.CreateCall(rt("jky_list_len"), {recv.val}), {tc.int_()}};
            }
            if (method == "append") {
                if (c->args.size() != 1) { wrongArgs(1); return {}; }
                TV a = emitExpr(c->args[0].get());
                Value* v = coerce(a, recv.type->elem, c->args[0]->loc);
                if (v) builder.CreateCall(rt("jky_list_append"),
                                          {recv.val, toSlot({v, recv.type->elem})});
                return {nullptr, {}};
            }
            break;
        case Ty::Map:
            if (method == "len") {
                if (!c->args.empty()) { wrongArgs(0); return {}; }
                return {builder.CreateCall(rt("jky_map_len"), {recv.val}), {tc.int_()}};
            }
            if (method == "has") {
                if (c->args.size() != 1) { wrongArgs(1); return {}; }
                TV a = emitExpr(c->args[0].get());
                Value* k = coerce(a, recv.type->key, c->args[0]->loc);
                Value* r = builder.CreateCall(rt("jky_map_has"),
                                              {recv.val, toSlot({k, recv.type->key})});
                return {builder.CreateTrunc(r, i1), {tc.bool_()}};
            }
            break;
        default: break;
    }
    err(c->loc, "E0371",
        "type '" + typeName(recv.type) + "' has no method '" + method + "'");
    return {};
}

// ---- statements -----------------------------------------------------------

} // namespace jocky
