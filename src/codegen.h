#pragma once
#include "ast.h"
#include "diagnostic.h"
#include "types.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvm {
class LLVMContext;
class Module;
class Function;
class Value;
class Type;
class StructType;
}

namespace jocky {

// Combined type checker + LLVM IR generator. Emits into an llvm::Module.
// Type errors and codegen errors are collected in diagnostics(); when any are
// present the module should not be used.
class CodeGen {
public:
    CodeGen(const Program& prog, std::string filename);
    ~CodeGen();

    bool run(); // true on success (no diagnostics)

    const std::vector<Diagnostic>& diagnostics() const { return diags_; }

    // Writes textual IR (.ll) to path. Returns false on error.
    bool writeIR(const std::string& path);
    // Emits a native object file to path (uses the host target). false on error.
    bool writeObject(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<Diagnostic> diags_;
};

} // namespace jocky
