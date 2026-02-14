// LexerFuzz.cpp — libFuzzer harness for the Liva lexer
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"

#include <cstdint>
#include <string>

using namespace liva;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 65536) return 0;

    std::string source(reinterpret_cast<const char *>(data), size);
    SourceManager sm("fuzz.liva", std::move(source));
    DiagnosticsEngine diag(&sm);

    Lexer lexer(sm, diag);
    auto tokens = lexer.lexAll();
    (void)tokens;

    return 0;
}
