#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jocky {

enum class Ty {
    Int, Float, Byte, Bool, Void,      // value types
    String, Bytes, List, Map, Struct,  // reference types (Error is a Struct)
    Nil,                               // type of the 'nil' literal
    UntypedInt, UntypedFloat,          // literal constants, adapt to context (D4)
    Error,                             // sentinel for a failed check (not user-visible)
};

struct StructDecl; // fwd (ast.h)

struct Type {
    Ty kind;
    // List: elem;  Map: key + elem(value)
    Type* elem = nullptr;
    Type* key = nullptr;
    // Struct: resolved declaration + name
    std::string name;
    const StructDecl* decl = nullptr;

    bool isValue() const {
        switch (kind) {
            case Ty::Int: case Ty::Float: case Ty::Byte: case Ty::Bool:
            case Ty::Void: case Ty::UntypedInt: case Ty::UntypedFloat:
                return true;
            default: return false;
        }
    }
    bool isReference() const {
        switch (kind) {
            case Ty::String: case Ty::Bytes: case Ty::List:
            case Ty::Map: case Ty::Struct:
                return true;
            default: return false;
        }
    }
    bool isUntyped() const { return kind == Ty::UntypedInt || kind == Ty::UntypedFloat; }
};

std::string typeName(const Type* t);

// Owns and interns Type objects.
class TypeContext {
public:
    TypeContext();
    Type* int_();
    Type* float_();
    Type* byte_();
    Type* bool_();
    Type* void_();
    Type* string_();
    Type* bytes_();
    Type* nil_();
    Type* untypedInt();
    Type* untypedFloat();
    Type* error_(); // the failure sentinel

    Type* list(Type* elem);
    Type* map(Type* key, Type* val);
    Type* struct_(const std::string& name, const StructDecl* decl);

private:
    Type* make(Ty k);
    std::vector<std::unique_ptr<Type>> pool_;
    Type *int_t{}, *float_t{}, *byte_t{}, *bool_t{}, *void_t{}, *string_t{},
         *bytes_t{}, *nil_t{}, *uint_t{}, *ufloat_t{}, *err_t{};
    std::unordered_map<std::string, Type*> structs_;
};

} // namespace jocky
