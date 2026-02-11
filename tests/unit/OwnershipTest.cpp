#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

class OwnershipTest : public ::testing::Test {
protected:
    struct CheckResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool passed;
    };

    CheckResult check(const std::string &source) {
        CheckResult result;
        result.sm = std::make_unique<SourceManager>("test.liva", source);
        result.diag.setSourceManager(result.sm.get());
        Lexer lexer(*result.sm, result.diag);
        Parser parser(lexer, result.diag);
        result.tu = parser.parseTranslationUnit();

        if (result.diag.hasErrors()) {
            result.passed = false;
            return result;
        }

        Sema sema(result.diag);
        result.passed = sema.analyze(*result.tu);
        return result;
    }

    bool hasDiag(const CheckResult &result, DiagID id) {
        for (auto &d : result.diag.getDiagnostics()) {
            if (d.id == id)
                return true;
        }
        return false;
    }
};

TEST_F(OwnershipTest, ImmutableAssignment) {
    auto result = check(R"(
        func main() {
            let x: i32 = 42
            x = 10
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_to_immutable));
}

TEST_F(OwnershipTest, MutableAssignment) {
    auto result = check(R"(
        func main() {
            var x: i32 = 42
            x = 10
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, ValidImmutableBorrow) {
    auto result = check(R"(
        func read(data: ref i32) {
            println(data)
        }
        func main() {
            let x: i32 = 42
            read(ref x)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, MutRefToImmutable) {
    auto result = check(R"(
        func main() {
            let x: i32 = 42
            let r = ref mut x
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_mut_ref_to_immutable));
}

TEST_F(OwnershipTest, ValidScopeExit) {
    auto result = check(R"(
        func main() {
            var x: i32 = 42
            {
                var y: i32 = x
                println(y)
            }
            println(x)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, SimpleProgram) {
    auto result = check(R"(
        func add(a: i32, b: i32) -> i32 {
            return a + b
        }

        func main() {
            let result = add(3, 4)
            println(result)
        }
    )");
    EXPECT_TRUE(result.passed);
}

// === Lifetime Analysis Tests ===

TEST_F(OwnershipTest, BorrowOutlivesValueInnerScope) {
    // ref assigned from inner scope to outer variable — should fail
    auto result = check(R"--(
        func main() {
            var x: i32 = 10
            var r = ref x
            {
                var y: i32 = 42
                r = ref y
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_borrow_outlives_value));
}

TEST_F(OwnershipTest, BorrowSameScope) {
    // ref and value in same scope — should pass
    auto result = check(R"(
        func main() {
            var x: i32 = 42
            let r = ref x
            println(r)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, BorrowOuterToInner) {
    // ref in inner scope to outer value — should pass (outer lives longer)
    auto result = check(R"(
        func main() {
            var x: i32 = 42
            {
                let r = ref x
                println(r)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}
