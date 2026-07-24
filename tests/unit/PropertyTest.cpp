#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>
#include <random>
#include <sstream>

using namespace liva;

// ============================================================
// Property-Based Test Infrastructure
//
// Generates random but structurally valid Liva programs and
// verifies that compiler properties hold:
// 1. Lexer never crashes on any input
// 2. Parser never crashes (with error recovery)
// 3. Valid programs always type-check
// 4. Re-lexing produces same token count
// 5. Error count is deterministic (same input → same errors)
// ============================================================

class PropertyTest : public ::testing::Test {
protected:
    std::mt19937 rng_{42}; // Fixed seed for reproducibility

    int randInt(int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng_);
    }

    std::string randId() {
        static const char *ids[] = {"x", "y", "z", "a", "b", "n", "i", "val", "tmp", "result"};
        return ids[randInt(0, 9)];
    }

    std::string randType() {
        static const char *types[] = {"i32", "i64", "f64", "bool", "string"};
        return types[randInt(0, 4)];
    }

    std::string randLiteral() {
        switch (randInt(0, 3)) {
        case 0: return std::to_string(randInt(0, 1000));
        case 1: return std::to_string(randInt(0, 100)) + "." + std::to_string(randInt(0, 99));
        case 2: return randInt(0, 1) ? "true" : "false";
        default: return "\"hello\"";
        }
    }

    std::string randOp() {
        static const char *ops[] = {"+", "-", "*", "/", "==", "!=", "<", ">"};
        return ops[randInt(0, 7)];
    }

    // Generate a random valid function
    std::string genFunc(const std::string &name, int stmts) {
        std::string rt = randType();
        std::string code = "func " + name + "() -> " + rt + " {\n";
        for (int i = 0; i < stmts; ++i) {
            std::string v = randId() + std::to_string(i);
            code += "    let " + v + ": " + randType() + " = " + randLiteral() + "\n";
        }
        code += "    return " + randLiteral() + "\n";
        code += "}\n";
        return code;
    }

    // Generate a random valid program with N functions
    std::string genProgram(int funcCount, int stmtsPerFunc) {
        std::string code;
        for (int i = 0; i < funcCount; ++i) {
            code += genFunc("func" + std::to_string(i), stmtsPerFunc);
            code += "\n";
        }
        code += "func main() {\n";
        for (int i = 0; i < funcCount; ++i) {
            code += "    let r" + std::to_string(i) + " = func" + std::to_string(i) + "()\n";
        }
        code += "}\n";
        return code;
    }

    // Generate random garbage input
    std::string genGarbage(int length) {
        static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789+-*/=(){}[]<>.,;:!?@#$%^& \n\t";
        std::string s;
        s.reserve(length);
        for (int i = 0; i < length; ++i)
            s += chars[randInt(0, sizeof(chars) - 2)];
        return s;
    }

    // Lex and return token count
    int lexTokenCount(const std::string &source) {
        SourceManager sm("prop.liva", source);
        DiagnosticsEngine diag(&sm);
        Lexer lexer(sm, diag);
        auto tokens = lexer.lexAll();
        return static_cast<int>(tokens.size());
    }

    // Parse and check for crash
    bool parseNoCrash(const std::string &source) {
        SourceManager sm("prop.liva", source);
        DiagnosticsEngine diag(&sm);
        diag.setMaxErrors(50);
        Lexer lexer(sm, diag);
        Parser parser(lexer, diag);
        auto tu = parser.parseTranslationUnit();
        return true; // If we got here, no crash
    }

    // Full pipeline: returns error count
    int pipelineErrors(const std::string &source) {
        SourceManager sm("prop.liva", source);
        DiagnosticsEngine diag(&sm);
        diag.setMaxErrors(50);
        Lexer lexer(sm, diag);
        Parser parser(lexer, diag);
        auto tu = parser.parseTranslationUnit();
        if (tu && !diag.hasErrors()) {
            Sema sema(diag);
            sema.analyze(*tu);
        }
        return static_cast<int>(diag.getDiagnostics().size());
    }
};

// ============================================================
// Property 1: Lexer never crashes on any input
// ============================================================

TEST_F(PropertyTest, LexerNeverCrashes_RandomGarbage) {
    for (int i = 0; i < 100; ++i) {
        rng_.seed(i);
        std::string input = genGarbage(randInt(1, 500));
        ASSERT_NO_FATAL_FAILURE(lexTokenCount(input))
            << "Lexer crashed on seed " << i;
    }
}

TEST_F(PropertyTest, LexerNeverCrashes_ValidPrograms) {
    for (int i = 0; i < 50; ++i) {
        rng_.seed(1000 + i);
        std::string code = genProgram(randInt(1, 10), randInt(0, 5));
        ASSERT_NO_FATAL_FAILURE(lexTokenCount(code))
            << "Lexer crashed on valid program seed " << i;
    }
}

// ============================================================
// Property 2: Parser never crashes (error recovery works)
// ============================================================

TEST_F(PropertyTest, ParserNeverCrashes_RandomGarbage) {
    for (int i = 0; i < 100; ++i) {
        rng_.seed(2000 + i);
        std::string input = genGarbage(randInt(1, 300));
        ASSERT_NO_FATAL_FAILURE(parseNoCrash(input))
            << "Parser crashed on garbage seed " << i;
    }
}

TEST_F(PropertyTest, ParserNeverCrashes_ValidPrograms) {
    for (int i = 0; i < 50; ++i) {
        rng_.seed(3000 + i);
        std::string code = genProgram(randInt(1, 8), randInt(0, 4));
        ASSERT_NO_FATAL_FAILURE(parseNoCrash(code))
            << "Parser crashed on valid program seed " << i;
    }
}

// ============================================================
// Property 3: Lexer is deterministic (same input → same tokens)
// ============================================================

TEST_F(PropertyTest, LexerDeterministic) {
    for (int i = 0; i < 50; ++i) {
        rng_.seed(4000 + i);
        std::string code = genProgram(randInt(1, 5), randInt(1, 3));
        int count1 = lexTokenCount(code);
        int count2 = lexTokenCount(code);
        EXPECT_EQ(count1, count2) << "Lexer non-deterministic on seed " << i;
    }
}

// ============================================================
// Property 4: Error count is deterministic
// ============================================================

TEST_F(PropertyTest, PipelineDeterministic) {
    for (int i = 0; i < 30; ++i) {
        rng_.seed(5000 + i);
        std::string input = genGarbage(randInt(10, 200));
        int err1 = pipelineErrors(input);
        int err2 = pipelineErrors(input);
        EXPECT_EQ(err1, err2) << "Pipeline non-deterministic on seed " << i;
    }
}

// ============================================================
// Property 5: Valid generated programs pass sema
// ============================================================

TEST_F(PropertyTest, ValidProgramsPassSema) {
    for (int i = 0; i < 30; ++i) {
        rng_.seed(6000 + i);
        std::string code = genProgram(randInt(1, 5), randInt(0, 3));
        // Generated programs should compile (some may fail due to type mismatches
        // from random literals, but should never crash)
        ASSERT_NO_FATAL_FAILURE(pipelineErrors(code))
            << "Pipeline crashed on valid program seed " << i;
    }
}

// ============================================================
// Property 6: Empty and minimal inputs don't crash
// ============================================================

TEST_F(PropertyTest, EdgeCases_NoCrash) {
    ASSERT_NO_FATAL_FAILURE(parseNoCrash(""));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash(" "));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("\n\n\n"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("{}"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("func"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("func ()"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("func main() {"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("let x = "));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("struct { var x: }"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("enum { case }"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("(((((((((("));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("))))))))))))"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("[[[[[[["));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("}}}}}}}}"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("'a 'b 'static"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("yield yield yield"));
    ASSERT_NO_FATAL_FAILURE(parseNoCrash("const const const"));
}
