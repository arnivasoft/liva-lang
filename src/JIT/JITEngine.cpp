#include "liva/JIT/JITEngine.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>

// Include runtime header for symbol addresses
#include "runtime.h"

namespace liva {

JITEngine::JITEngine(std::unique_ptr<llvm::orc::LLJIT> jit)
    : jit_(std::move(jit)) {}

JITEngine::~JITEngine() = default;

/// Register all liva_runtime symbols as absolute symbols in the main JITDylib.
/// This is needed on Windows where statically linked symbols are not visible
/// via GetForCurrentProcess/GetProcAddress.
static void registerRuntimeSymbols(llvm::orc::LLJIT &jit) {
    auto &es = jit.getExecutionSession();
    auto &mainDylib = jit.getMainJITDylib();

    llvm::orc::SymbolMap symbols;
    auto flags = llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable;

    #define REG(name) \
        symbols[es.intern(#name)] = { \
            llvm::orc::ExecutorAddr::fromPtr(reinterpret_cast<void*>(&name)), flags }

    // Registration set generated from the normative RuntimeFunctions.def
    // table: only LIVA_RT_JIT / LIVA_RT_JIT_VA entries expand to REG(name);
    // the other two macros (LIVA_RT / LIVA_RT_VA, IRGen-decl-only) and
    // RT_ARGS are defined empty here since the JIT does not need type
    // codes to register a symbol address.
    #define RT_ARGS(...)
    #define LIVA_RT(name, ret, params)
    #define LIVA_RT_VA(name, ret, params)
    #define LIVA_RT_JIT(name, ret, params)    REG(name);
    #define LIVA_RT_JIT_VA(name, ret, params) REG(name);
    #include "../IR/RuntimeFunctions.def"
    #undef LIVA_RT
    #undef LIVA_RT_JIT
    #undef LIVA_RT_VA
    #undef LIVA_RT_JIT_VA
    #undef RT_ARGS

    // The following 26 names were registered in the original hand-written
    // REG block but are NOT declared via RuntimeFunctions.def: 11 are LIVE
    // symbols declared outside createRuntimeDecls in declareAsyncRuntimeFuncs
    // (the 7 liva_task_* plus liva_coro_resume/liva_coro_destroy/
    // liva_scheduler_run/liva_async_sleep — do NOT remove these), and 15 are
    // apparently-dead registrations with no IRGen decl or call site anywhere
    // in src/ (liva_alloc, liva_free, liva_alloc_zeroed, liva_string_free,
    // liva_string_compare, liva_print_i32/i64/f64/bool/str, liva_println_str,
    // liva_http_response_free/status/body, liva_args_free). Kept verbatim for
    // behavior preservation; cleaning up the 15 dead ones is a separately-
    // tracked follow-up.
    // tabloda yok — elle korunuyor (bkz. task raporu)
    REG(liva_alloc);
    REG(liva_free);
    REG(liva_alloc_zeroed);
    REG(liva_string_free);
    REG(liva_string_compare);
    REG(liva_print_i32);
    REG(liva_print_i64);
    REG(liva_print_f64);
    REG(liva_print_bool);
    REG(liva_print_str);
    REG(liva_println_str);
    REG(liva_http_response_free);
    REG(liva_http_response_status);
    REG(liva_http_response_body);
    REG(liva_task_complete);
    REG(liva_task_is_done);
    REG(liva_task_get_handle);
    REG(liva_task_set_parent);
    REG(liva_task_destroy);
    REG(liva_task_cancel);
    REG(liva_task_is_cancelled);
    REG(liva_coro_resume);
    REG(liva_coro_destroy);
    REG(liva_scheduler_run);
    REG(liva_async_sleep);
    REG(liva_args_free);

    #undef REG

    llvm::cantFail(mainDylib.define(llvm::orc::absoluteSymbols(std::move(symbols))));
}

std::unique_ptr<JITEngine> JITEngine::create() {
    // Initialize native target (idempotent)
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto builder = llvm::orc::LLJITBuilder();
    auto jit = builder.create();
    if (!jit) {
        llvm::consumeError(jit.takeError());
        return nullptr;
    }

    auto &mainDylib = (*jit)->getMainJITDylib();
    // Add process symbols as fallback (works on Linux/macOS, partial on Windows)
    auto gen = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        (*jit)->getDataLayout().getGlobalPrefix());
    if (!gen) {
        llvm::consumeError(gen.takeError());
        return nullptr;
    }
    mainDylib.addGenerator(std::move(*gen));

    // Register all runtime symbols as absolute symbols (needed on Windows)
    registerRuntimeSymbols(**jit);

    return std::unique_ptr<JITEngine>(new JITEngine(std::move(*jit)));
}

int JITEngine::evaluate(std::unique_ptr<llvm::LLVMContext> ctx,
                         std::unique_ptr<llvm::Module> module,
                         std::string &errorOut) {
    // Create a fresh JITDylib per evaluation to avoid symbol conflicts
    std::string dlName = "__repl_eval_" + std::to_string(evalCounter_++);
    auto &es = jit_->getExecutionSession();
    auto dl = es.createJITDylib(dlName);
    if (!dl) {
        errorOut = llvm::toString(dl.takeError());
        return -1;
    }

    // Link against main dylib for process symbols (runtime functions)
    dl->addToLinkOrder(jit_->getMainJITDylib());

    // Add module to the fresh dylib
    auto tsm = llvm::orc::ThreadSafeModule(std::move(module), std::move(ctx));
    if (auto err = jit_->addIRModule(*dl, std::move(tsm))) {
        errorOut = llvm::toString(std::move(err));
        return -1;
    }

    // Look up main
    auto mainSym = jit_->lookup(*dl, "main");
    if (!mainSym) {
        errorOut = llvm::toString(mainSym.takeError());
        return -1;
    }

    // Execute main
    auto *mainFn = mainSym->toPtr<int()>();
    int result = mainFn();

    // Clean up dylib
    if (auto err = es.removeJITDylib(*dl))
        llvm::consumeError(std::move(err));

    return result;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
