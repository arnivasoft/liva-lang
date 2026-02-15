#include "liva/CodeGen/TargetInfo.h"

#ifdef LIVA_HAS_LLVM
#include <llvm/TargetParser/Host.h>
#endif

namespace liva {

TargetInfo TargetInfo::getHostTarget() {
    TargetInfo info;
#ifdef LIVA_HAS_LLVM
    info.triple = llvm::sys::getDefaultTargetTriple();
    info.cpu = "generic";
    info.features = "";
#else
    info.triple = "unknown-unknown-unknown";
    info.cpu = "generic";
    info.features = "";
#endif
    return info;
}

TargetInfo TargetInfo::fromTriple(const std::string &triple,
                                  const std::string &cpu,
                                  const std::string &features) {
    TargetInfo info;
    info.triple = triple;
    info.cpu = cpu;
    info.features = features;
    return info;
}

bool TargetInfo::isCrossCompiling() const {
    auto host = getHostTarget();
    return triple != host.triple;
}

bool TargetInfo::isWasm() const {
    return triple.find("wasm32") == 0 || triple.find("wasm64") == 0;
}

} // namespace liva
