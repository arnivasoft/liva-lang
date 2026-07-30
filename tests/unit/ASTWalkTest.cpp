// ASTWalkTest: forEachChild/walkSubtree'nin AST'nin her köşesine gerçekten
// ulaştığını davranış üzerinden doğrular. switch'in eksiksizliği -Wswitch'e
// bırakıldığı için burada düğüm türü SAYMIYORUZ — ulaşılabilirliği ölçüyoruz.

#include "liva/AST/ASTWalk.h"
#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"

#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <string>

using namespace liva;

namespace {

class ASTWalkTest : public ::testing::Test {
protected:
    std::unique_ptr<SourceManager> sm;
    DiagnosticsEngine diag;
    std::unique_ptr<TranslationUnit> tu;

    /// Kaynağı parse eder ve bildirimlerden walkSubtree ile ulaşılan TÜM
    /// identifier adlarını döndürür. walkSubtree false dönerse test patlar.
    std::multiset<std::string> identifiers(const std::string &source) {
        sm = std::make_unique<SourceManager>("astwalk.liva", source);
        diag.setSourceManager(sm.get());
        Lexer lexer(*sm, diag);
        Parser parser(lexer, diag);
        tu = parser.parseTranslationUnit();
        EXPECT_FALSE(diag.hasErrors()) << "parse başarısız";

        std::multiset<std::string> names;
        for (auto &d : tu->getDeclarations()) {
            bool complete = walkSubtree(d.get(), [&](const ASTNode *n) {
                if (n->getKind() == ASTNode::NodeKind::IdentifierExpr)
                    names.insert(
                        static_cast<const IdentifierExpr *>(n)->getName());
            });
            EXPECT_TRUE(complete) << "walkSubtree tanınmayan düğüm bildirdi";
        }
        return names;
    }

    static bool has(const std::multiset<std::string> &names,
                    const std::string &name) {
        return names.count(name) > 0;
    }
};

TEST_F(ASTWalkTest, MemberAndIndexChainsAreReached) {
    auto names = identifiers(R"--(
        struct P { x: i32 }
        func main() {
            let p = P { x: 1 }
            var arr: [i32] = [1, 2, 3]
            let i: i32 = 0
            println(p.x)
            println(arr[i])
        }
    )--");
    EXPECT_TRUE(has(names, "p"));
    EXPECT_TRUE(has(names, "arr"));
    EXPECT_TRUE(has(names, "i"));
}

TEST_F(ASTWalkTest, ClosureBodyIsReached) {
    auto names = identifiers(R"--(
        func main() {
            let k: i32 = 1
            let f = |x: i32| -> i32 { return x + k }
            println(f(2))
        }
    )--");
    EXPECT_TRUE(has(names, "k")) << "closure gövdesine ulaşılmadı";
}

TEST_F(ASTWalkTest, TernaryAndUnaryAreReached) {
    auto names = identifiers(R"--(
        func main() {
            let a: i32 = 1
            let b: i32 = 2
            let c: bool = true
            println(c ? a : -b)
        }
    )--");
    EXPECT_TRUE(has(names, "a"));
    EXPECT_TRUE(has(names, "b"));
    EXPECT_TRUE(has(names, "c"));
}

TEST_F(ASTWalkTest, StructLiteralFieldValuesAreReached) {
    auto names = identifiers(R"--(
        struct P {
            x: i32
            y: i32
        }
        func main() {
            let a: i32 = 1
            let p = P { x: a, y: 2 }
            println(p.x)
        }
    )--");
    EXPECT_TRUE(has(names, "a")) << "struct literali alan değerine ulaşılmadı";
}

TEST_F(ASTWalkTest, StringInterpolationIsReached) {
    // Parser "\(x)" ifadesini BinaryExpr(Add, StringLiteral,
    // CallExpr(toString, x))'e şekerden arındırıyor, yani özel işlem gerekmez.
    auto names = identifiers(R"--(
        func main() {
            let x: i32 = 7
            println("v=\(x)")
        }
    )--");
    EXPECT_TRUE(has(names, "x"));
    EXPECT_TRUE(has(names, "toString"));
}

TEST_F(ASTWalkTest, NullChildrenDoNotCrash) {
    // Değersiz return ve else'siz if — nullable çocuklar.
    auto names = identifiers(R"--(
        func main() {
            let c: bool = true
            if c {
                return
            }
        }
    )--");
    EXPECT_TRUE(has(names, "c"));
}

TEST_F(ASTWalkTest, WhileAndForBodiesAreReached) {
    auto names = identifiers(R"--(
        func main() {
            var n: i32 = 0
            let limit: i32 = 3
            while n < limit {
                n = n + 1
            }
            for i in 0..3 {
                println(n)
            }
        }
    )--");
    EXPECT_TRUE(has(names, "limit"));
    EXPECT_TRUE(has(names, "n"));
}

TEST_F(ASTWalkTest, MethodBodiesInsideImplAreReached) {
    auto names = identifiers(R"--(
        struct C { v: i32 }
        impl C {
            func get(self) -> i32 {
                return self.v
            }
        }
        func main() {
            let c = C { v: 1 }
            println(c.get())
        }
    )--");
    EXPECT_TRUE(has(names, "self")) << "impl metot gövdesine ulaşılmadı";
}

} // namespace
