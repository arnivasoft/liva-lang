#pragma once

namespace liva {

// Type codes used by src/IR/RuntimeFunctions.def (LIVA_RT / LIVA_RT_JIT
// macros) to describe runtime ABI parameter/return types without pulling
// in LLVM headers. Kept intentionally minimal — only the types actually
// used by runtime function signatures.
enum RtTypeCode { RT_VOID, RT_PTR, RT_I8, RT_I32, RT_I64, RT_F64 };

} // namespace liva
