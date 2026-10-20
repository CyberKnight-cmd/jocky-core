#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace jocky;

namespace {

void printUsage(const char* prog) {
    std::fprintf(stderr,
        "jockyc — the JOCKY compiler\n"
        "usage: %s [options] <file.jky>\n"
        "options:\n"
        "  --emit-tokens   lex the file and print the token stream\n"
        "  --emit-ast      parse the file and print the AST\n"
        "  --emit-llvm     write LLVM IR to <output>.ll\n"
        "  -o <file>       output executable name\n"
        "  -h, --help      show this help\n",
        prog);
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default: out += c;
        }
    }
    out += '"';
    return out;
}

void printTokens(const std::vector<Token>& toks) {
    for (const Token& t : toks) {
        std::printf("%4u:%-3u  %-10s", t.line, t.col, tokName(t.kind));
        switch (t.kind) {
            case Tok::IntLit:
            case Tok::CharLit:
                std::printf("  %lld", (long long)t.intVal);
                break;
            case Tok::FloatLit:
                std::printf("  %g", t.floatVal);
                break;
            case Tok::StringLit:
                std::printf("  %s%s", quote(t.text).c_str(),
                            t.hasInterp ? "  [interp]" : "");
                break;
            case Tok::HexBytes:
                std::printf("  x\"%s\"", t.text.c_str());
                break;
            case Tok::Ident:
            case Tok::Error:
                std::printf("  %s", t.text.c_str());
                break;
            default:
                break;
        }
        std::printf("\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string path;
    std::string output;
    bool emitTokens = false;
    bool emitAst = false;
    bool emitLLVM = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--emit-tokens") {
            emitTokens = true;
        } else if (arg == "--emit-ast") {
            emitAst = true;
        } else if (arg == "--emit-llvm") {
            emitLLVM = true;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "jockyc: -o requires an argument\n");
                return 2;
            }
            output = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "jockyc: unknown option '%s'\n", arg.c_str());
            return 2;
        } else {
            path = arg;
        }
    }

    if (path.empty()) {
        printUsage(argv[0]);
        return 2;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "jockyc: cannot open '%s'\n", path.c_str());
        return 1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();

    Lexer lexer(source, path);
    std::vector<Token> toks = lexer.tokenize();

    auto report = [&](const std::vector<Diagnostic>& ds) {
        for (const Diagnostic& d : ds)
            std::fprintf(stderr, "%s:%u:%u: error[%s]: %s\n", path.c_str(),
                         d.line, d.col, d.code.c_str(), d.message.c_str());
    };

    report(lexer.diagnostics());
    if (emitTokens) printTokens(toks);
    if (lexer.hadError()) return 1;

    Parser parser(std::move(toks), path);
    Program prog = parser.parseProgram();
    report(parser.diagnostics());
    if (emitAst) {
        std::fputs(dumpAst(prog).c_str(), stdout);
        return parser.hadError() ? 1 : 0;
    }
    if (parser.hadError()) return 1;

    // --- code generation ---
    CodeGen cg(prog, path);
    bool genOk = cg.run();
    report(cg.diagnostics());
    if (!genOk) return 1;

    // Default output name: input basename without extension.
    if (output.empty()) {
        std::string base = path;
        size_t slash = base.find_last_of('/');
        if (slash != std::string::npos) base = base.substr(slash + 1);
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        output = base.empty() ? "a.out" : base;
    }

    if (emitLLVM) {
        std::string llPath = output + ".ll";
        if (!cg.writeIR(llPath)) { report(cg.diagnostics()); return 1; }
        std::fprintf(stderr, "wrote %s\n", llPath.c_str());
        return 0;
    }

    // Emit an object file, then link it with the runtime into an executable.
    std::string objPath = output + ".o";
    if (!cg.writeObject(objPath)) { report(cg.diagnostics()); return 1; }

    std::string cmd = std::string(JOCKY_CC) + " '" + objPath + "' '" +
                      JOCKY_RT_LIB + "' -lgc -o '" + output + "'";
    int rc = std::system(cmd.c_str());
    std::remove(objPath.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "jockyc: link step failed (exit %d)\n", rc);
        return 1;
    }
    return 0;
}
