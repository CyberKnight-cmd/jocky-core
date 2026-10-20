#include "types.h"

namespace jocky {

std::string typeName(const Type* t) {
    if (!t) return "<?>";
    switch (t->kind) {
        case Ty::Int: return "int";
        case Ty::Float: return "float";
        case Ty::Byte: return "byte";
        case Ty::Bool: return "bool";
        case Ty::Void: return "void";
        case Ty::String: return "string";
        case Ty::Bytes: return "bytes";
        case Ty::List: return "list[" + typeName(t->elem) + "]";
        case Ty::Map: return "map[" + typeName(t->key) + ", " + typeName(t->elem) + "]";
        case Ty::Struct: return t->name;
        case Ty::Nil: return "nil";
        case Ty::UntypedInt: return "untyped int";
        case Ty::UntypedFloat: return "untyped float";
        case Ty::Error: return "<error>";
    }
    return "<?>";
}

TypeContext::TypeContext() {
    int_t = make(Ty::Int);
    float_t = make(Ty::Float);
    byte_t = make(Ty::Byte);
    bool_t = make(Ty::Bool);
    void_t = make(Ty::Void);
    string_t = make(Ty::String);
    bytes_t = make(Ty::Bytes);
    nil_t = make(Ty::Nil);
    uint_t = make(Ty::UntypedInt);
    ufloat_t = make(Ty::UntypedFloat);
    err_t = make(Ty::Error);
}

Type* TypeContext::make(Ty k) {
    pool_.push_back(std::make_unique<Type>());
    pool_.back()->kind = k;
    return pool_.back().get();
}

Type* TypeContext::int_() { return int_t; }
Type* TypeContext::float_() { return float_t; }
Type* TypeContext::byte_() { return byte_t; }
Type* TypeContext::bool_() { return bool_t; }
Type* TypeContext::void_() { return void_t; }
Type* TypeContext::string_() { return string_t; }
Type* TypeContext::bytes_() { return bytes_t; }
Type* TypeContext::nil_() { return nil_t; }
Type* TypeContext::untypedInt() { return uint_t; }
Type* TypeContext::untypedFloat() { return ufloat_t; }
Type* TypeContext::error_() { return err_t; }

Type* TypeContext::list(Type* elem) {
    Type* t = make(Ty::List);
    t->elem = elem;
    return t;
}
Type* TypeContext::map(Type* key, Type* val) {
    Type* t = make(Ty::Map);
    t->key = key;
    t->elem = val;
    return t;
}
Type* TypeContext::struct_(const std::string& name, const StructDecl* decl) {
    auto it = structs_.find(name);
    if (it != structs_.end()) {
        if (decl && !it->second->decl) it->second->decl = decl;
        return it->second;
    }
    Type* t = make(Ty::Struct);
    t->kind = Ty::Struct;
    t->name = name;
    t->decl = decl;
    structs_[name] = t;
    return t;
}

} // namespace jocky
