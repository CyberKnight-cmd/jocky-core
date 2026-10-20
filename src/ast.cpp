#include "ast.h"
#include <sstream>

namespace jocky {

namespace {
struct Printer {
    std::ostringstream os;
    int indent = 0;
    void pad() { for (int i = 0; i < indent; i++) os << "  "; }
    void line(const std::string& s) { pad(); os << s << "\n"; }

    std::string typeStr(const TypeExpr* t) {
        if (!t) return "auto";
        switch (t->kind) {
            case TypeKind::Named: return t->name;
            case TypeKind::List:  return "list[" + typeStr(t->elem.get()) + "]";
            case TypeKind::Map:   return "map[" + typeStr(t->key.get()) + ", " +
                                          typeStr(t->elem.get()) + "]";
            case TypeKind::Auto:  return "auto";
        }
        return "?";
    }

    void expr(const Expr* e) {
        if (!e) { line("<null>"); return; }
        switch (e->kind) {
            case ExprKind::IntLit:
                line("Int " + std::to_string(((const IntLitExpr*)e)->value)); break;
            case ExprKind::FloatLit: {
                std::ostringstream t; t << ((const FloatLitExpr*)e)->value;
                line("Float " + t.str()); break;
            }
            case ExprKind::BoolLit:
                line(std::string("Bool ") + (((const BoolLitExpr*)e)->value ? "true" : "false")); break;
            case ExprKind::NilLit: line("Nil"); break;
            case ExprKind::CharLit:
                line("Char " + std::to_string(((const CharLitExpr*)e)->value)); break;
            case ExprKind::StringLit:
                line("String \"" + ((const StringLitExpr*)e)->value + "\""); break;
            case ExprKind::InterpString: {
                auto* s = (const InterpStringExpr*)e;
                line("InterpString");
                indent++;
                for (size_t i = 0; i < s->exprs.size(); i++) {
                    line("lit \"" + s->literals[i] + "\"");
                    expr(s->exprs[i].get());
                }
                line("lit \"" + s->literals.back() + "\"");
                indent--;
                break;
            }
            case ExprKind::HexBytesLit: {
                auto* h = (const HexBytesLitExpr*)e;
                line("HexBytes [" + std::to_string(h->bytes.size()) + " bytes]"); break;
            }
            case ExprKind::ListLit: {
                auto* a = (const ListLitExpr*)e;
                line("ListLit"); indent++;
                for (auto& x : a->elems) expr(x.get());
                indent--; break;
            }
            case ExprKind::MapLit: {
                auto* m = (const MapLitExpr*)e;
                line("MapLit"); indent++;
                for (size_t i = 0; i < m->keys.size(); i++) {
                    line("key"); indent++; expr(m->keys[i].get()); indent--;
                    line("val"); indent++; expr(m->values[i].get()); indent--;
                }
                indent--; break;
            }
            case ExprKind::StructLit: {
                auto* s = (const StructLitExpr*)e;
                line("StructLit " + s->typeName); indent++;
                for (size_t i = 0; i < s->fieldNames.size(); i++) {
                    line("." + s->fieldNames[i]); indent++;
                    expr(s->fieldValues[i].get()); indent--;
                }
                indent--; break;
            }
            case ExprKind::Ident:
                line("Ident " + ((const IdentExpr*)e)->name); break;
            case ExprKind::Unary: {
                auto* u = (const UnaryExpr*)e;
                line(std::string("Unary ") + unOpName(u->op)); indent++;
                expr(u->operand.get()); indent--; break;
            }
            case ExprKind::Binary: {
                auto* b = (const BinaryExpr*)e;
                line(std::string("Binary ") + binOpName(b->op)); indent++;
                expr(b->lhs.get()); expr(b->rhs.get()); indent--; break;
            }
            case ExprKind::Cast: {
                auto* c = (const CastExpr*)e;
                line("Cast (" + typeStr(c->target.get()) + ")"); indent++;
                expr(c->operand.get()); indent--; break;
            }
            case ExprKind::Call: {
                auto* c = (const CallExpr*)e;
                line("Call"); indent++;
                line("callee"); indent++; expr(c->callee.get()); indent--;
                for (auto& a : c->args) { line("arg"); indent++; expr(a.get()); indent--; }
                indent--; break;
            }
            case ExprKind::Member: {
                auto* m = (const MemberExpr*)e;
                line("Member ." + m->field); indent++;
                expr(m->object.get()); indent--; break;
            }
            case ExprKind::Index: {
                auto* ix = (const IndexExpr*)e;
                line("Index"); indent++;
                expr(ix->object.get()); expr(ix->index.get()); indent--; break;
            }
        }
    }

    void block(const Block& b) {
        indent++;
        for (auto& s : b.stmts) stmt(s.get());
        indent--;
    }

    void varDecl(const VarDeclStmt* v) {
        std::string names;
        for (size_t i = 0; i < v->names.size(); i++) {
            if (i) names += ", ";
            names += v->names[i];
        }
        line(std::string(v->isConst ? "Const " : "Var ") +
             typeStr(v->declType.get()) + " " + names);
        indent++; expr(v->init.get()); indent--;
    }

    void stmt(const Stmt* s) {
        switch (s->kind) {
            case StmtKind::VarDecl: varDecl((const VarDeclStmt*)s); break;
            case StmtKind::Assign: {
                auto* a = (const AssignStmt*)s;
                line(std::string("Assign ") +
                     (a->isCompound ? std::string(binOpName(a->compound)) + "=" : "="));
                indent++;
                line("target"); indent++; expr(a->target.get()); indent--;
                line("value"); indent++; expr(a->value.get()); indent--;
                indent--; break;
            }
            case StmtKind::ExprStmt:
                line("ExprStmt"); indent++;
                expr(((const ExprStmt*)s)->expr.get()); indent--; break;
            case StmtKind::If: {
                auto* i = (const IfStmt*)s;
                line("If"); indent++;
                line("cond"); indent++; expr(i->cond.get()); indent--;
                line("then"); block(i->thenBlk);
                if (i->hasElse) {
                    if (i->elseIf) { line("else-if"); indent++; stmt(i->elseIf.get()); indent--; }
                    else { line("else"); block(i->elseBlk); }
                }
                indent--; break;
            }
            case StmtKind::While: {
                auto* w = (const WhileStmt*)s;
                line("While"); indent++;
                line("cond"); indent++; expr(w->cond.get()); indent--;
                line("body"); block(w->body);
                indent--; break;
            }
            case StmtKind::For: {
                auto* f = (const ForStmt*)s;
                line("For " + f->var + " in"); indent++;
                expr(f->iterable.get());
                line("body"); block(f->body);
                indent--; break;
            }
            case StmtKind::Return: {
                auto* r = (const ReturnStmt*)s;
                line("Return"); indent++;
                for (auto& v : r->values) expr(v.get());
                indent--; break;
            }
            case StmtKind::Block:
                line("Block"); block(((const BlockStmt*)s)->block); break;
        }
    }

    void program(const Program& p) {
        for (auto& i : p.imports) line("Import " + i.module);
        for (auto& g : p.globals) varDecl(g.get());
        for (auto& s : p.structs) {
            line("Struct " + s->name); indent++;
            for (auto& f : s->fields) line(typeStr(f.type.get()) + " " + f.name);
            indent--;
        }
        for (auto& fn : p.functions) {
            std::string sig = "Fn " + fn->name + "(";
            for (size_t i = 0; i < fn->params.size(); i++) {
                if (i) sig += ", ";
                sig += typeStr(fn->params[i].type.get()) + " " + fn->params[i].name;
            }
            sig += ")";
            if (!fn->returns.empty()) {
                sig += " -> ";
                for (size_t i = 0; i < fn->returns.size(); i++) {
                    if (i) sig += ", ";
                    sig += typeStr(fn->returns[i].get());
                }
            }
            line(sig);
            block(fn->body);
        }
    }
};
} // namespace

std::string dumpAst(const Program& p) {
    Printer pr;
    pr.program(p);
    return pr.os.str();
}

} // namespace jocky
