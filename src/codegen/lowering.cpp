// Type lowering and conversions: mapping JOCKY types to LLVM types, resolving
// syntactic type expressions, the coercion rules (no implicit conversions, D4
// untyped literals), runtime-function signatures, and list/map slot packing.
#include "codegen/internal.h"

using namespace llvm;

namespace jocky {

llvm::Type* CodeGen::Impl::lower(Type* t) {
    switch (t->kind) {
        case Ty::Int: case Ty::UntypedInt: return i64;
        case Ty::Float: case Ty::UntypedFloat: return dbl;
        case Ty::Byte: return i8;
        case Ty::Bool: return i1;
        case Ty::Void: return voidTy;
        case Ty::String: case Ty::Bytes: case Ty::List:
        case Ty::Map: case Ty::Struct: case Ty::Nil:
            return ptrTy;
        case Ty::Error: return ptrTy;
    }
    return ptrTy;
}

llvm::Type* CodeGen::Impl::fnRetType(const std::vector<Type*>& rets) {
    if (rets.empty() || (rets.size() == 1 && rets[0]->kind == Ty::Void))
        return i64; // JOCKY main/void functions still return i64 0 at the ABI
    if (rets.size() == 1) return lower(rets[0]);
    std::vector<llvm::Type*> elems;
    for (auto* r : rets) elems.push_back(lower(r));
    return StructType::get(ctx, elems);
}

llvm::FunctionType* CodeGen::Impl::rtSig(const std::string& n) {
    auto FT = [&](llvm::Type* r, std::vector<llvm::Type*> a) {
        return FunctionType::get(r, a, false);
    };
    if (n == "jky_alloc") return FT(ptrTy, {i64});
    if (n == "jky_str_new") return FT(ptrTy, {ptrTy, i64});
    if (n == "jky_str_concat") return FT(ptrTy, {ptrTy, ptrTy});
    if (n == "jky_str_eq") return FT(i8, {ptrTy, ptrTy});
    if (n == "jky_str_len") return FT(i64, {ptrTy});
    if (n == "jky_str_contains") return FT(i8, {ptrTy, ptrTy});
    if (n == "jky_str_slice") return FT(ptrTy, {ptrTy, i64, i64});
    if (n == "jky_str_from_int") return FT(ptrTy, {i64});
    if (n == "jky_str_from_float") return FT(ptrTy, {dbl});
    if (n == "jky_str_from_bool") return FT(ptrTy, {i8});
    if (n == "jky_str_from_byte") return FT(ptrTy, {i8});
    if (n == "jky_bytes_new") return FT(ptrTy, {ptrTy, i64});
    if (n == "jky_bytes_len") return FT(i64, {ptrTy});
    if (n == "jky_bytes_get") return FT(i8, {ptrTy, i64});
    if (n == "jky_bytes_set") return FT(voidTy, {ptrTy, i64, i8});
    if (n == "jky_bytes_slice") return FT(ptrTy, {ptrTy, i64, i64});
    if (n == "jky_bytes_eq") return FT(i8, {ptrTy, ptrTy});
    if (n == "jky_list_new") return FT(ptrTy, {});
    if (n == "jky_list_append") return FT(voidTy, {ptrTy, i64});
    if (n == "jky_list_get") return FT(i64, {ptrTy, i64});
    if (n == "jky_list_set") return FT(voidTy, {ptrTy, i64, i64});
    if (n == "jky_list_len") return FT(i64, {ptrTy});
    if (n == "jky_map_new") return FT(ptrTy, {i32});
    if (n == "jky_map_set") return FT(voidTy, {ptrTy, i64, i64});
    if (n == "jky_map_get") return FT(i64, {ptrTy, i64, ptrTy});
    if (n == "jky_map_has") return FT(i8, {ptrTy, i64});
    if (n == "jky_map_len") return FT(i64, {ptrTy});
    if (n == "jky_log_info" || n == "jky_log_warn" || n == "jky_log_error")
        return FT(voidTy, {ptrTy});
    if (n == "jky_fs_read") return FT(StructType::get(ctx, {ptrTy, ptrTy}), {ptrTy});
    if (n == "jky_err_new") return FT(ptrTy, {i64, ptrTy});
    if (n == "jky_panic") return FT(voidTy, {ptrTy, i32});
    if (n == "jky_nil_check") return FT(voidTy, {ptrTy, i32});
    return FT(voidTy, {});
}

Type* CodeGen::Impl::resolveType(const TypeExpr* te) {
    if (!te) return tc.error_();
    switch (te->kind) {
        case TypeKind::Auto: return tc.error_();
        case TypeKind::List: return tc.list(resolveType(te->elem.get()));
        case TypeKind::Map:
            return tc.map(resolveType(te->key.get()), resolveType(te->elem.get()));
        case TypeKind::Named: {
            const std::string& n = te->name;
            if (n == "int") return tc.int_();
            if (n == "float") return tc.float_();
            if (n == "byte") return tc.byte_();
            if (n == "bool") return tc.bool_();
            if (n == "void") return tc.void_();
            if (n == "string") return tc.string_();
            if (n == "bytes") return tc.bytes_();
            auto it = structs.find(n);
            if (it != structs.end()) return tc.struct_(n, it->second.decl);
            err(te->loc, "E0300", "unknown type '" + n + "'");
            return tc.error_();
        }
    }
    return tc.error_();
}

// Produce a Value of `target` type from tv, or report an error and return null.
llvm::Value* CodeGen::Impl::coerce(TV tv, Type* target, Loc l) {
    if (!tv.val || !tv.type) return nullptr;
    Type* s = tv.type;
    if (s->kind == Ty::Error || target->kind == Ty::Error) return tv.val;

    // nil -> reference type
    if (s->kind == Ty::Nil) {
        if (target->isReference()) return ConstantPointerNull::get(ptrTy);
        err(l, "E0006", "cannot assign 'nil' to value type '" + typeName(target) + "'");
        return nullptr;
    }
    // untyped integer literal (adapts to context, D4)
    if (s->kind == Ty::UntypedInt) {
        if (target->kind == Ty::Int) return tv.val;
        if (target->kind == Ty::Float) {
            if (auto* c = dyn_cast<ConstantInt>(tv.val))
                return ConstantFP::get(dbl, (double)c->getSExtValue());
            return builder.CreateSIToFP(tv.val, dbl);
        }
        if (target->kind == Ty::Byte) {
            if (auto* c = dyn_cast<ConstantInt>(tv.val)) {
                int64_t v = c->getSExtValue();
                if (v < 0 || v > 255) {
                    err(l, "E0008", "literal " + std::to_string(v) +
                                    " out of range for 'byte'");
                    return nullptr;
                }
                return ConstantInt::get(i8, (uint64_t)v);
            }
            return builder.CreateTrunc(tv.val, i8);
        }
        err(l, "E0007", "cannot use integer literal as '" + typeName(target) + "'");
        return nullptr;
    }
    if (s->kind == Ty::UntypedFloat) {
        if (target->kind == Ty::Float) return tv.val;
        err(l, "E0007", "cannot use float literal as '" + typeName(target) + "'");
        return nullptr;
    }
    // exact match
    if (sameType(s, target)) return tv.val;

    err(l, "E0007", "cannot use '" + typeName(s) + "' where '" +
                    typeName(target) + "' is expected (no implicit conversion)");
    return nullptr;
}

bool CodeGen::Impl::sameType(Type* a, Type* b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case Ty::List: return sameType(a->elem, b->elem);
        case Ty::Map: return sameType(a->key, b->key) && sameType(a->elem, b->elem);
        case Ty::Struct: return a->name == b->name;
        default: return true; // primitives/string/bytes: kind match suffices
    }
}

Type* CodeGen::Impl::concretize(Type* t) {
    if (t->kind == Ty::UntypedInt) return tc.int_();
    if (t->kind == Ty::UntypedFloat) return tc.float_();
    return t;
}

llvm::Value* CodeGen::Impl::toStringValue(TV tv, Loc l) {
    switch (tv.type->kind) {
        case Ty::String: return tv.val;
        case Ty::Int: case Ty::UntypedInt:
            return builder.CreateCall(rt("jky_str_from_int"), {tv.val});
        case Ty::Float: case Ty::UntypedFloat:
            return builder.CreateCall(rt("jky_str_from_float"), {tv.val});
        case Ty::Bool:
            return builder.CreateCall(rt("jky_str_from_bool"),
                                      {builder.CreateZExt(tv.val, i8)});
        case Ty::Byte:
            return builder.CreateCall(rt("jky_str_from_byte"), {tv.val});
        default:
            err(l, "E0310", "cannot convert '" + typeName(tv.type) + "' to string");
            return ConstantPointerNull::get(ptrTy);
    }
}

// List/map elements are stored as raw 8-byte slots; pack/unpack by element type.
llvm::Value* CodeGen::Impl::toSlot(TV tv) {
    switch (tv.type->kind) {
        case Ty::Int: case Ty::UntypedInt: return tv.val;
        case Ty::Byte: return builder.CreateZExt(tv.val, i64);
        case Ty::Bool: return builder.CreateZExt(tv.val, i64);
        case Ty::Float: case Ty::UntypedFloat: return builder.CreateBitCast(tv.val, i64);
        default: return builder.CreatePtrToInt(tv.val, i64); // references
    }
}
TV CodeGen::Impl::fromSlot(llvm::Value* slot, Type* t) {
    switch (t->kind) {
        case Ty::Int: return {slot, t};
        case Ty::Byte: return {builder.CreateTrunc(slot, i8), t};
        case Ty::Bool: return {builder.CreateTrunc(slot, i1), t};
        case Ty::Float: return {builder.CreateBitCast(slot, dbl), t};
        default: return {builder.CreateIntToPtr(slot, ptrTy), t};
    }
}

} // namespace jocky
