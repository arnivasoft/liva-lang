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

} // namespace liva
