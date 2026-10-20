// Top-level code-generation driver: the public CodeGen facade, target machine
// setup, the run() pipeline, and writing textual IR / native object files.
#include "codegen/internal.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

using namespace llvm;

namespace jocky {

bool CodeGen::Impl::initTarget() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    Triple triple(sys::getDefaultTargetTriple());
    std::string errStr;
    const Target* t = TargetRegistry::lookupTarget(triple, errStr);
    if (!t) { err({0, 0}, "E0400", "no target: " + errStr); return false; }
    TargetOptions opt;
    tm.reset(t->createTargetMachine(triple, "generic", "", opt,
                                    std::optional<Reloc::Model>(Reloc::PIC_)));
    mod->setTargetTriple(triple);
    mod->setDataLayout(tm->createDataLayout());
    return true;
}

bool CodeGen::Impl::run() {
    if (!initTarget()) return false;
    if (!declareStructsAndFns()) return false;
    pushScope(); // persistent global scope (holds top-level consts)
    if (!prog.globals.empty()) declareGlobals();
    for (auto& fn : prog.functions) {
        if (funcs.count(fn->name)) emitFunction(fn.get());
    }
    if (!ok()) return false;
    std::string vErr;
    raw_string_ostream os(vErr);
    if (verifyModule(*mod, &os)) {
        err({0, 0}, "E0401", "internal: invalid IR: " + vErr);
        return false;
    }
    return true;
}

// ---- public facade --------------------------------------------------------
CodeGen::CodeGen(const Program& prog, std::string filename) {
    impl_ = std::make_unique<Impl>(prog, std::move(filename), &diags_);
}
CodeGen::~CodeGen() = default;

bool CodeGen::run() { return impl_->run(); }

bool CodeGen::writeIR(const std::string& path) {
    std::error_code ec;
    raw_fd_ostream out(path, ec, sys::fs::OF_None);
    if (ec) { diags_.push_back({"E0402", ec.message(), 0, 0}); return false; }
    impl_->mod->print(out, nullptr);
    return true;
}

bool CodeGen::writeObject(const std::string& path) {
    std::error_code ec;
    raw_fd_ostream out(path, ec, sys::fs::OF_None);
    if (ec) { diags_.push_back({"E0403", ec.message(), 0, 0}); return false; }
    legacy::PassManager pm;
    if (impl_->tm->addPassesToEmitFile(pm, out, nullptr, CodeGenFileType::ObjectFile)) {
        diags_.push_back({"E0404", "target cannot emit object file", 0, 0});
        return false;
    }
    pm.run(*impl_->mod);
    out.flush();
    return true;
}

} // namespace jocky
