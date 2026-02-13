#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <chrono>
#include <gtest/gtest.h>
#include <string>

using namespace liva;

// ============================================================
// Performance Regression Tests
//
// These tests verify that compiler phases complete within
// reasonable time bounds. They are not microbenchmarks —
// they catch severe performance regressions.
// ============================================================

class BenchmarkTest : public ::testing::Test {
protected:
    std::unique_ptr<SourceManager> sm_;
    DiagnosticsEngine diag_;

    std::vector<Token> lexSource(const std::string &source) {
        sm_ = std::make_unique<SourceManager>("bench.liva", source);
        diag_.clear();
        diag_.setSourceManager(sm_.get());
        Lexer lexer(*sm_, diag_);
        return lexer.lexAll();
    }

    std::unique_ptr<TranslationUnit> parseSource(const std::string &source) {
        sm_ = std::make_unique<SourceManager>("bench.liva", source);
        diag_.clear();
        diag_.setSourceManager(sm_.get());
        Lexer lexer(*sm_, diag_);
        Parser parser(lexer, diag_);
        return parser.parseTranslationUnit();
    }

    bool analyzeSource(const std::string &source) {
        sm_ = std::make_unique<SourceManager>("bench.liva", source);
        diag_.clear();
        diag_.setSourceManager(sm_.get());
        Lexer lexer(*sm_, diag_);
        Parser parser(lexer, diag_);
        auto tu = parser.parseTranslationUnit();
        if (!tu || diag_.hasErrors()) return false;
        Sema sema(diag_, nullptr);
        return sema.analyze(*tu);
    }

    // Generate N functions with control flow
    static std::string generateFunctions(int count) {
        std::string src;
        for (int i = 0; i < count; ++i) {
            src += "func fn" + std::to_string(i) +
                   "(a: i32, b: i32) -> i32 {\n"
                   "    if a > b {\n"
                   "        return a\n"
                   "    }\n"
                   "    return b\n"
                   "}\n\n";
        }
        src += "func main() {\n"
               "    let x = fn0(1, 2)\n"
               "}\n";
        return src;
    }

    // Generate N variable declarations
    static std::string generateVariables(int count) {
        std::string src = "func main() {\n";
        for (int i = 0; i < count; ++i) {
            src += "    let x" + std::to_string(i) +
                   ": i32 = " + std::to_string(i) + "\n";
        }
        src += "}\n";
        return src;
    }

    // Generate nested structs
    static std::string generateStructs(int count) {
        std::string src;
        for (int i = 0; i < count; ++i) {
            src += "struct S" + std::to_string(i) + " {\n"
                   "    var a: i32\n"
                   "    var b: i32\n"
                   "    var c: i32\n"
                   "}\n\n";
        }
        src += "func main() {\n"
               "    let s = S0 { a: 1, b: 2, c: 3 }\n"
               "}\n";
        return src;
    }

    template <typename Fn>
    double measureMs(Fn fn) {
        auto start = std::chrono::high_resolution_clock::now();
        fn();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
};

// === Lexer Performance ===

TEST_F(BenchmarkTest, Lexer50Functions) {
    auto src = generateFunctions(50);
    double ms = measureMs([&]() { lexSource(src); });
    EXPECT_LT(ms, 50.0) << "Lexing 50 functions took " << ms << "ms (limit: 50ms)";
}

TEST_F(BenchmarkTest, Lexer200Variables) {
    auto src = generateVariables(200);
    double ms = measureMs([&]() { lexSource(src); });
    EXPECT_LT(ms, 50.0) << "Lexing 200 variables took " << ms << "ms (limit: 50ms)";
}

TEST_F(BenchmarkTest, Lexer1000Lines) {
    std::string src;
    for (int i = 0; i < 1000; ++i) {
        src += "let v" + std::to_string(i) + " = " + std::to_string(i * 7) + "\n";
    }
    double ms = measureMs([&]() { lexSource(src); });
    EXPECT_LT(ms, 100.0) << "Lexing 1000 lines took " << ms << "ms (limit: 100ms)";
}

// === Parser Performance ===

TEST_F(BenchmarkTest, Parser50Functions) {
    auto src = generateFunctions(50);
    double ms = measureMs([&]() { parseSource(src); });
    EXPECT_LT(ms, 100.0) << "Parsing 50 functions took " << ms << "ms (limit: 100ms)";
}

TEST_F(BenchmarkTest, Parser200Variables) {
    auto src = generateVariables(200);
    double ms = measureMs([&]() { parseSource(src); });
    EXPECT_LT(ms, 100.0) << "Parsing 200 variables took " << ms << "ms (limit: 100ms)";
}

TEST_F(BenchmarkTest, Parser50Structs) {
    auto src = generateStructs(50);
    double ms = measureMs([&]() { parseSource(src); });
    EXPECT_LT(ms, 100.0) << "Parsing 50 structs took " << ms << "ms (limit: 100ms)";
}

// === Sema Performance ===

TEST_F(BenchmarkTest, Sema50Functions) {
    auto src = generateFunctions(50);
    double ms = measureMs([&]() { analyzeSource(src); });
    EXPECT_LT(ms, 200.0) << "Analyzing 50 functions took " << ms << "ms (limit: 200ms)";
}

TEST_F(BenchmarkTest, Sema200Variables) {
    auto src = generateVariables(200);
    double ms = measureMs([&]() { analyzeSource(src); });
    EXPECT_LT(ms, 200.0) << "Analyzing 200 variables took " << ms << "ms (limit: 200ms)";
}

TEST_F(BenchmarkTest, Sema50Structs) {
    auto src = generateStructs(50);
    double ms = measureMs([&]() { analyzeSource(src); });
    EXPECT_LT(ms, 200.0) << "Analyzing 50 structs took " << ms << "ms (limit: 200ms)";
}

// === Scaling Tests ===

TEST_F(BenchmarkTest, LexerScalingLinear) {
    // Verify lexer scales roughly linearly with input size
    auto small = generateFunctions(10);
    auto large = generateFunctions(100);

    double msSmall = measureMs([&]() { lexSource(small); });
    double msLarge = measureMs([&]() { lexSource(large); });

    // Large is 10x the functions; allow up to 20x time (generous for overhead)
    if (msSmall > 0.01) { // guard against too-fast measurements
        double ratio = msLarge / msSmall;
        EXPECT_LT(ratio, 20.0) << "Lexer scaling ratio: " << ratio
                                << " (expected < 20x for 10x input)";
    }
}

TEST_F(BenchmarkTest, ParserScalingLinear) {
    auto small = generateFunctions(10);
    auto large = generateFunctions(100);

    double msSmall = measureMs([&]() { parseSource(small); });
    double msLarge = measureMs([&]() { parseSource(large); });

    if (msSmall > 0.01) {
        double ratio = msLarge / msSmall;
        EXPECT_LT(ratio, 20.0) << "Parser scaling ratio: " << ratio
                                << " (expected < 20x for 10x input)";
    }
}

TEST_F(BenchmarkTest, SemaScalingLinear) {
    auto small = generateFunctions(10);
    auto large = generateFunctions(100);

    double msSmall = measureMs([&]() { analyzeSource(small); });
    double msLarge = measureMs([&]() { analyzeSource(large); });

    if (msSmall > 0.01) {
        double ratio = msLarge / msSmall;
        EXPECT_LT(ratio, 25.0) << "Sema scaling ratio: " << ratio
                                << " (expected < 25x for 10x input)";
    }
}

// === Throughput Tests ===

TEST_F(BenchmarkTest, LexerThroughputAbove1MBs) {
    // Lexer should process at least 1 MB/s on any reasonable hardware
    auto src = generateFunctions(100);
    double ms = measureMs([&]() { lexSource(src); });
    double bytesPerSec = (src.size() / (ms / 1000.0));
    double mbPerSec = bytesPerSec / 1'000'000.0;
    EXPECT_GT(mbPerSec, 1.0)
        << "Lexer throughput: " << mbPerSec << " MB/s (minimum: 1 MB/s)";
}

TEST_F(BenchmarkTest, PipelineThroughputAbove500KBs) {
    // Full pipeline should process at least 500 KB/s
    auto src = generateFunctions(100);
    double ms = measureMs([&]() { analyzeSource(src); });
    double bytesPerSec = (src.size() / (ms / 1000.0));
    double kbPerSec = bytesPerSec / 1000.0;
    EXPECT_GT(kbPerSec, 500.0)
        << "Pipeline throughput: " << kbPerSec << " KB/s (minimum: 500 KB/s)";
}
