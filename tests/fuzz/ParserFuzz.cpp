// ParserFuzz.cpp — libFuzzer harness for the Liva parser
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"

#include <cstdint>
#include <string>

using namespace liva;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 65536) return 0;

    std::string source(reinterpret_cast<const char *>(data), size);
    SourceManager sm("fuzz.liva", std::move(source));
    DiagnosticsEngine diag(&sm);
    diag.setMaxErrors(10);

    Lexer lexer(sm, diag);
    Parser parser(lexer, diag);
    auto tu = parser.parseTranslationUnit();
    (void)tu;

    return 0;
}
