#pragma once

#ifdef LIVA_HAS_LLVM

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <memory>
#include <string>

namespace liva {

class JITEngine {
public:
    /// Create a JIT engine. Returns nullptr on failure.
    static std::unique_ptr<JITEngine> create();

    /// Add a module and execute its main() function.
    /// Returns the exit code, or -1 on error.
    int evaluate(std::unique_ptr<llvm::LLVMContext> ctx,
                 std::unique_ptr<llvm::Module> module,
                 std::string &errorOut);

    ~JITEngine();

private:
    JITEngine(std::unique_ptr<llvm::orc::LLJIT> jit);
    std::unique_ptr<llvm::orc::LLJIT> jit_;
    int evalCounter_ = 0;
};

} // namespace liva

#endif // LIVA_HAS_LLVM
