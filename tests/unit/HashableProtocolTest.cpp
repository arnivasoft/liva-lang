// P1-8 Hash family alt-spec 1 — Hashable protocol declaration tests.
//
// Verifies that:
//   1. The Hashable protocol shape parses + type-checks.
//   2. A user struct can `impl T: Hashable` and call its hash() method.
//   3. A generic function `where T: Hashable` accepts conformers.
//
// Note: Hashable Sema enforcement and built-in conformance are intentionally
// scoped out — this iteration only proves the declaration shape works.

#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

class HashableProtocolTest : public ::testing::Test {
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
};

// ---------------------------------------------------------------------------
// The Hashable protocol declaration shape parses and type-checks.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, HashableProtocolParses) {
    auto result = check(R"--(
        protocol Hashable {
            func hash(self) -> i32
        }
        func main() {}
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// A user struct implementing Hashable can call its hash() method directly.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, UserTypeConformsToHashable) {
    auto result = check(R"--(
        protocol Hashable {
            func hash(self) -> i32
        }
        struct Point {
            var x: i32
            var y: i32
        }
        impl Point: Hashable {
            func hash(self) -> i32 {
                return self.x + self.y
            }
        }
        func main() {
            let p = Point { x: 3, y: 4 }
            let h = p.hash()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// A generic constraint `where T: Hashable` accepts conforming types.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, GenericConstraintAcceptsHashableConformer) {
    auto result = check(R"--(
        protocol Hashable {
            func hash(self) -> i32
        }
        struct K {
            var v: i32
        }
        impl K: Hashable {
            func hash(self) -> i32 {
                return self.v
            }
        }
        func bucket<T>(k: T) -> i32 where T: Hashable {
            return k.hash()
        }
        func main() {
            let b = bucket(K { v: 42 })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// Built-in primitives (i32, string, bool, ...) conform to Hashable
// implicitly. `where T: Hashable` accepts them in generic constraints.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, BuiltinPrimitivesAreHashable) {
    auto result = check(R"--(
        protocol Hashable {
            func hash() -> i64
        }
        func bucket<T>(k: T) -> i64 where T: Hashable {
            return k.hash()
        }
        func main() {
            let a = bucket(42)
            let b = bucket("foo")
            let c = bucket(true)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// Calling .hash() on a primitive resolves with i64 return type.
// ---------------------------------------------------------------------------
TEST_F(HashableProtocolTest, PrimitiveHashCallReturnsI64) {
    auto result = check(R"--(
        func main() {
            let x: i32 = 42
            let h: i64 = x.hash()
            let s = "abc"
            let g: i64 = s.hash()
        }
    )--");
    EXPECT_TRUE(result.passed);
}
