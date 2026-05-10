// Task 6: Sema for-in resolution — accept custom Iterator conformers.
//
// Tests that for-in over a struct that implements the Iterator protocol
// (using the new "Iterator" protocol name from Tasks 3-4) resolves correctly
// in the type-checker: the loop variable receives the element type from the
// impl's next() return type, and the for-in body type-checks without errors.
//
// Syntax notes (Liva):
//   - Protocol declaration: `protocol Iterator { mut func next() -> Item? }`
//   - Impl conformance:     `impl Counter: Iterator { ... }`
//   - Method with mutable self: `func next(mut self) -> T?`
//   - No `type Item = T` extraction used here; element type comes from
//     the return type of `next()` as extracted by visitImplDecl.

#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

class IteratorProtocolTest : public ::testing::Test {
protected:
    struct CheckResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool passed;
    };

    // Parse + sema-check a snippet. Returns passed=true iff no diagnostics
    // with severity >= Error were emitted.
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

// ---------------------------------------------------------------------------
// Basic: struct implementing Iterator, for-in resolves element type as i32.
// ---------------------------------------------------------------------------
TEST_F(IteratorProtocolTest, CustomIteratorConformsAndForInResolves) {
    auto result = check(R"--(
        protocol Iterator {
            func next(mut self) -> i32?
        }
        struct Counter {
            var n: i32
        }
        impl Counter: Iterator {
            func next(mut self) -> i32? {
                if self.n <= 0 { return nil }
                self.n = self.n - 1
                return self.n + 1
            }
        }
        func main() {
            var c = Counter { n: 3 }
            for x in c {
                let v: i32 = x
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// Element type is string: for-in loop variable should be usable as string.
// ---------------------------------------------------------------------------
TEST_F(IteratorProtocolTest, CustomIteratorStringElement) {
    auto result = check(R"--(
        protocol Iterator {
            func next(mut self) -> string?
        }
        struct Words {
            var idx: i32
        }
        impl Words: Iterator {
            func next(mut self) -> string? {
                if self.idx <= 0 { return nil }
                self.idx = self.idx - 1
                return "word"
            }
        }
        func main() {
            var w = Words { idx: 2 }
            for item in w {
                let s: string = item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// No conformance: a plain struct in for-in must emit err_for_in_not_iterable.
// ---------------------------------------------------------------------------
TEST_F(IteratorProtocolTest, NonIterableEmitsDiagnostic) {
    auto result = check(R"--(
        struct Plain {
            var x: i32
        }
        func main() {
            var p = Plain { x: 0 }
            for item in p {
                let v = item
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_for_in_not_iterable));
}

// ---------------------------------------------------------------------------
// Multiple conformers: both types resolve their element types independently.
// ---------------------------------------------------------------------------
TEST_F(IteratorProtocolTest, MultipleCustomIteratorConformers) {
    auto result = check(R"--(
        protocol Iterator {
            func next(mut self) -> i32?
        }
        struct Up {
            var n: i32
        }
        impl Up: Iterator {
            func next(mut self) -> i32? {
                if self.n <= 0 { return nil }
                self.n = self.n - 1
                return self.n
            }
        }
        struct Down {
            var n: i32
        }
        impl Down: Iterator {
            func next(mut self) -> i32? {
                if self.n <= 0 { return nil }
                self.n = self.n - 1
                return self.n
            }
        }
        func main() {
            var a = Up { n: 3 }
            for x in a { let i: i32 = x }
            var b = Down { n: 3 }
            for y in b { let j: i32 = y }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// Loop body can do arithmetic with the element type (requires type binding).
// ---------------------------------------------------------------------------
TEST_F(IteratorProtocolTest, CustomIteratorBodyArithmetic) {
    auto result = check(R"--(
        protocol Iterator {
            func next(mut self) -> i32?
        }
        struct Seq {
            var pos: i32
        }
        impl Seq: Iterator {
            func next(mut self) -> i32? {
                if self.pos <= 0 { return nil }
                self.pos = self.pos - 1
                return self.pos
            }
        }
        func main() {
            var s = Seq { pos: 5 }
            for item in s {
                let doubled: i32 = item + item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ---------------------------------------------------------------------------
// Generic function with `where I: Iterator, I.Item == i32` constraint:
// the body's `it.next()` resolves against the protocol method, returning Item?,
// and Item == i32 lets the body type-check.
// ---------------------------------------------------------------------------
TEST_F(IteratorProtocolTest, GenericIteratorConstraintWithItemEquality) {
    auto result = check(R"--(
        protocol Iterator {
            func next(mut self) -> i32?
        }
        struct Counter { var n: i32 }
        impl Counter: Iterator {
            func next(mut self) -> i32? {
                if self.n <= 0 { return nil }
                self.n = self.n - 1
                return self.n + 1
            }
        }
        func sum<I>(iter: I) -> i32 where I: Iterator, I.Item == i32 {
            var total: i32 = 0
            var it = iter
            while let x = it.next() { total = total + x }
            return total
        }
        func main() {
            let s = sum(Counter { n: 3 })
        }
    )--");
    EXPECT_TRUE(result.passed);
}
