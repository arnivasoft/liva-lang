#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

using namespace liva;

// ============================================================
// Benchmark Harness
// ============================================================

struct BenchResult {
    std::string name;
    size_t iterations;
    double minUs;       // microseconds
    double maxUs;
    double avgUs;
    double medianUs;
    double opsPerSec;
    size_t inputSize;   // bytes of source code
};

static BenchResult runBench(const std::string &name, size_t iterations,
                            size_t inputSize, std::function<void()> fn) {
    // Warmup
    for (size_t i = 0; i < 3; ++i)
        fn();

    std::vector<double> timings;
    timings.reserve(iterations);

    for (size_t i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        fn();
        auto end = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(end - start).count();
        timings.push_back(us);
    }

    std::sort(timings.begin(), timings.end());

    BenchResult r;
    r.name = name;
    r.iterations = iterations;
    r.inputSize = inputSize;
    r.minUs = timings.front();
    r.maxUs = timings.back();
    r.avgUs = std::accumulate(timings.begin(), timings.end(), 0.0) /
              static_cast<double>(timings.size());
    r.medianUs = timings[timings.size() / 2];
    r.opsPerSec = (r.avgUs > 0.0) ? (1'000'000.0 / r.avgUs) : 0.0;
    return r;
}

static void printResult(const BenchResult &r) {
    std::cout << std::left << std::setw(38) << r.name
              << std::right << std::setw(8) << r.iterations << " iters"
              << "  avg=" << std::fixed << std::setprecision(1)
              << std::setw(10) << r.avgUs << " us"
              << "  med=" << std::setw(10) << r.medianUs << " us"
              << "  min=" << std::setw(10) << r.minUs << " us"
              << "  ops/s=" << std::setprecision(0)
              << std::setw(10) << r.opsPerSec;
    if (r.inputSize > 0) {
        double mbPerSec = (r.inputSize / 1'000'000.0) * r.opsPerSec;
        std::cout << "  " << std::setprecision(2) << mbPerSec << " MB/s";
    }
    std::cout << "\n";
}

static void printHeader() {
    std::cout << "\n"
              << "================================================================"
              << "===========================\n"
              << "  Liva Compiler Benchmarks\n"
              << "================================================================"
              << "===========================\n\n";
}

static void printSection(const std::string &name) {
    std::cout << "--- " << name << " ---\n";
}

// ============================================================
// Test Source Code Generators
// ============================================================

static std::string makeSmallSource() {
    return "func add(x: i32, y: i32) -> i32 {\n"
           "    return x\n"
           "}\n"
           "\n"
           "func main() {\n"
           "    let result = add(1, 2)\n"
           "}\n";
}

static std::string makeMediumSource() {
    std::string src;
    src += "struct Point {\n"
           "    var x: f64\n"
           "    var y: f64\n"
           "}\n\n";

    src += "enum Shape {\n"
           "    case Circle(f64)\n"
           "    case Rectangle(f64, f64)\n"
           "    case Triangle(f64, f64, f64)\n"
           "}\n\n";

    src += "protocol Drawable {\n"
           "    func draw(self) -> string\n"
           "}\n\n";

    // Generate 10 functions
    for (int i = 0; i < 10; ++i) {
        src += "func compute_" + std::to_string(i) +
               "(a: i32, b: i32) -> i32 {\n"
               "    let x = a\n"
               "    let y = b\n"
               "    if x > y {\n"
               "        return x\n"
               "    } else {\n"
               "        return y\n"
               "    }\n"
               "}\n\n";
    }

    src += "func main() {\n"
           "    var total: i32 = 0\n"
           "    let p = Point { x: 1.0, y: 2.0 }\n";
    for (int i = 0; i < 10; ++i) {
        src += "    total = compute_" + std::to_string(i) +
               "(total, " + std::to_string(i) + ")\n";
    }
    src += "}\n";
    return src;
}

static std::string makeLargeSource() {
    std::string src;

    // 5 structs
    for (int s = 0; s < 5; ++s) {
        src += "struct Data" + std::to_string(s) + " {\n";
        for (int f = 0; f < 5; ++f) {
            src += "    var field" + std::to_string(f) + ": i32\n";
        }
        src += "}\n\n";
    }

    // 5 enums
    for (int e = 0; e < 5; ++e) {
        src += "enum Kind" + std::to_string(e) + " {\n";
        for (int c = 0; c < 4; ++c) {
            src += "    case Variant" + std::to_string(c) + "(i32)\n";
        }
        src += "}\n\n";
    }

    // 50 functions with control flow
    for (int i = 0; i < 50; ++i) {
        src += "func process_" + std::to_string(i) +
               "(x: i32, y: i32, z: i32) -> i32 {\n"
               "    var result: i32 = 0\n"
               "    if x > 0 {\n"
               "        result = x\n"
               "    } else {\n"
               "        result = y\n"
               "    }\n"
               "    let temp = result\n"
               "    var i: i32 = 0\n"
               "    while i < z {\n"
               "        result = result + temp\n"
               "        i = i + 1\n"
               "    }\n"
               "    return result\n"
               "}\n\n";
    }

    // main calling all functions
    src += "func main() {\n"
           "    var acc: i32 = 0\n";
    for (int i = 0; i < 50; ++i) {
        src += "    acc = process_" + std::to_string(i) +
               "(acc, " + std::to_string(i) + ", 5)\n";
    }
    src += "}\n";
    return src;
}

static std::string makeStressTokens() {
    // Lots of diverse tokens: operators, numbers, strings, keywords
    std::string src;
    for (int i = 0; i < 100; ++i) {
        src += "let x" + std::to_string(i) + ": i32 = " +
               std::to_string(i * 7) + " + " + std::to_string(i * 3) +
               " - " + std::to_string(i) + "\n";
    }
    return src;
}

static std::string makeDeepNesting() {
    std::string src = "func deep(x: i32) -> i32 {\n";
    for (int i = 0; i < 15; ++i) {
        std::string indent(4 * (i + 1), ' ');
        src += indent + "if x > " + std::to_string(i) + " {\n";
    }
    std::string innerIndent(4 * 16, ' ');
    src += innerIndent + "return x\n";
    for (int i = 14; i >= 0; --i) {
        std::string indent(4 * (i + 1), ' ');
        src += indent + "}\n";
    }
    src += "    return 0\n}\n";
    return src;
}

static std::string makeManyDeclarations() {
    std::string src;
    for (int i = 0; i < 100; ++i) {
        src += "func fn" + std::to_string(i) + "(a: i32) -> i32 {\n"
               "    return a\n"
               "}\n\n";
    }
    src += "func main() {\n"
           "    let x = fn0(1)\n"
           "}\n";
    return src;
}

// ============================================================
// Benchmark Functions
// ============================================================

static void benchLexer(const std::string &name, const std::string &source,
                       size_t iterations, std::vector<BenchResult> &results) {
    SourceManager sm("bench.liva", source);
    DiagnosticsEngine diag(&sm);

    auto r = runBench(name, iterations, source.size(), [&]() {
        Lexer lexer(sm, diag);
        auto tokens = lexer.lexAll();
        (void)tokens;
    });
    printResult(r);
    results.push_back(r);
}

static void benchParser(const std::string &name, const std::string &source,
                        size_t iterations, std::vector<BenchResult> &results) {
    SourceManager sm("bench.liva", source);
    DiagnosticsEngine diag(&sm);

    auto r = runBench(name, iterations, source.size(), [&]() {
        diag.clear();
        Lexer lexer(sm, diag);
        Parser parser(lexer, diag);
        auto tu = parser.parseTranslationUnit();
        (void)tu;
    });
    printResult(r);
    results.push_back(r);
}

static void benchSema(const std::string &name, const std::string &source,
                      size_t iterations, std::vector<BenchResult> &results) {
    SourceManager sm("bench.liva", source);
    DiagnosticsEngine diag(&sm);

    auto r = runBench(name, iterations, source.size(), [&]() {
        diag.clear();
        Lexer lexer(sm, diag);
        Parser parser(lexer, diag);
        auto tu = parser.parseTranslationUnit();
        if (tu) {
            Sema sema(diag, nullptr);
            sema.analyze(*tu);
        }
    });
    printResult(r);
    results.push_back(r);
}

static void benchPipeline(const std::string &name, const std::string &source,
                          size_t iterations, std::vector<BenchResult> &results) {
    // Full frontend pipeline: Lex → Parse → Sema
    benchSema(name, source, iterations, results);
}

// ============================================================
// Main
// ============================================================

int main() {
    printHeader();

    std::vector<BenchResult> results;

    // Prepare source inputs
    auto smallSrc = makeSmallSource();
    auto mediumSrc = makeMediumSource();
    auto largeSrc = makeLargeSource();
    auto tokenStress = makeStressTokens();
    auto deepNesting = makeDeepNesting();
    auto manyDecls = makeManyDeclarations();

    std::cout << "Input sizes:\n"
              << "  small:     " << smallSrc.size() << " bytes\n"
              << "  medium:    " << mediumSrc.size() << " bytes\n"
              << "  large:     " << largeSrc.size() << " bytes\n"
              << "  tokens:    " << tokenStress.size() << " bytes\n"
              << "  nesting:   " << deepNesting.size() << " bytes\n"
              << "  decls:     " << manyDecls.size() << " bytes\n\n";

    // --- Lexer Benchmarks ---
    printSection("Lexer");
    benchLexer("Lexer/small",           smallSrc,     5000, results);
    benchLexer("Lexer/medium",          mediumSrc,    2000, results);
    benchLexer("Lexer/large",           largeSrc,     500,  results);
    benchLexer("Lexer/token_stress",    tokenStress,  2000, results);
    benchLexer("Lexer/deep_nesting",    deepNesting,  3000, results);
    benchLexer("Lexer/many_decls",      manyDecls,    1000, results);
    std::cout << "\n";

    // --- Parser Benchmarks ---
    printSection("Parser (Lex + Parse)");
    benchParser("Parser/small",         smallSrc,     5000, results);
    benchParser("Parser/medium",        mediumSrc,    2000, results);
    benchParser("Parser/large",         largeSrc,     500,  results);
    benchParser("Parser/deep_nesting",  deepNesting,  3000, results);
    benchParser("Parser/many_decls",    manyDecls,    1000, results);
    std::cout << "\n";

    // --- Sema Benchmarks ---
    printSection("Sema (Lex + Parse + TypeCheck + Ownership + Lifetime)");
    benchSema("Sema/small",             smallSrc,     3000, results);
    benchSema("Sema/medium",            mediumSrc,    1000, results);
    benchSema("Sema/large",             largeSrc,     200,  results);
    benchSema("Sema/deep_nesting",      deepNesting,  2000, results);
    benchSema("Sema/many_decls",        manyDecls,    500,  results);
    std::cout << "\n";

    // --- Full Pipeline Benchmarks ---
    printSection("Full Pipeline (Lex + Parse + Sema)");
    benchPipeline("Pipeline/small",     smallSrc,     3000, results);
    benchPipeline("Pipeline/medium",    mediumSrc,    1000, results);
    benchPipeline("Pipeline/large",     largeSrc,     200,  results);
    std::cout << "\n";

    // --- Summary ---
    std::cout << "================================================================"
              << "===========================\n"
              << "  Summary (" << results.size() << " benchmarks)\n"
              << "================================================================"
              << "===========================\n\n";

    // Find fastest and slowest throughput
    double maxMBs = 0.0;
    std::string maxMBsName;
    for (const auto &r : results) {
        if (r.inputSize > 0) {
            double mbps = (r.inputSize / 1'000'000.0) * r.opsPerSec;
            if (mbps > maxMBs) {
                maxMBs = mbps;
                maxMBsName = r.name;
            }
        }
    }
    if (!maxMBsName.empty()) {
        std::cout << "  Peak throughput: " << std::fixed << std::setprecision(2)
                  << maxMBs << " MB/s (" << maxMBsName << ")\n";
    }

    // Lexer-only throughput for large input
    for (const auto &r : results) {
        if (r.name == "Lexer/large" && r.inputSize > 0) {
            double mbps = (r.inputSize / 1'000'000.0) * r.opsPerSec;
            std::cout << "  Lexer throughput (large): " << std::setprecision(2)
                      << mbps << " MB/s ("
                      << std::setprecision(0) << r.opsPerSec << " ops/s)\n";
        }
        if (r.name == "Sema/large" && r.inputSize > 0) {
            double mbps = (r.inputSize / 1'000'000.0) * r.opsPerSec;
            std::cout << "  Full analysis (large): " << std::setprecision(2)
                      << mbps << " MB/s ("
                      << std::setprecision(0) << r.opsPerSec << " ops/s)\n";
        }
    }

    std::cout << "\n";
    return 0;
}
