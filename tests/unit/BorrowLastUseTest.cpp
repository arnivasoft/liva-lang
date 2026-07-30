// BorrowLastUseTest: findLastUse'un deyim indeksi ve dört geri-çekilme
// kuralının birim testleri. Yalnız parse eder — Sema koşmaz, çünkü test edilen
// şey saf sözdizimsel bir tarama.

#include "liva/Sema/BorrowLastUse.h"

#include "liva/AST/Decl.h"
#include "liva/AST/Stmt.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace liva;

namespace {

class BorrowLastUseTest : public ::testing::Test {
protected:
    std::unique_ptr<SourceManager> sm;
    DiagnosticsEngine diag;
    std::unique_ptr<TranslationUnit> tu;

    /// Kaynaktaki İLK bildirimin (bir `func`) gövde deyimlerini döndürür.
    const std::vector<std::unique_ptr<ASTNode>> &bodyOf(const std::string &src) {
        sm = std::make_unique<SourceManager>("lastuse.liva", src);
        diag.setSourceManager(sm.get());
        Lexer lexer(*sm, diag);
        Parser parser(lexer, diag);
        tu = parser.parseTranslationUnit();
        EXPECT_FALSE(diag.hasErrors()) << "parse başarısız";

        auto *fn = static_cast<FuncDecl *>(tu->getDeclarations()[0].get());
        return fn->getBody()->getStatements();
    }
};

TEST_F(BorrowLastUseTest, LastMentionSetsTheIndex) {
    // 0: var k    1: let r = ref k    2: println(r)    3: k = 42
    const auto &stmts = bodyOf(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            println(r)
            k = 42
        }
    )--");
    auto lu = findLastUse(stmts, 1, "r");
    EXPECT_TRUE(lu.shortenable);
    EXPECT_EQ(lu.stmtIndex, 2u);
}

TEST_F(BorrowLastUseTest, NoLaterUseReleasesAtTheDecl) {
    const auto &stmts = bodyOf(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            k = 42
            println(k)
        }
    )--");
    auto lu = findLastUse(stmts, 1, "r");
    EXPECT_TRUE(lu.shortenable);
    EXPECT_EQ(lu.stmtIndex, 1u) << "hiç kullanım yoksa bildirimde bırakılmalı";
}

TEST_F(BorrowLastUseTest, LoopStatementItselfIsTheLastUse) {
    // Döngü gövdesindeki kullanım, döngü DEYİMİNİ son kullanım yapar —
    // bırakma noktası döngü tamamen bittikten sonra, bu yüzden geri kenar
    // sorun değil.
    const auto &stmts = bodyOf(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            for i in 0..3 {
                println(r)
            }
            k = 42
        }
    )--");
    auto lu = findLastUse(stmts, 1, "r");
    EXPECT_TRUE(lu.shortenable);
    EXPECT_EQ(lu.stmtIndex, 2u);
}

TEST_F(BorrowLastUseTest, NestedBlockMentionCountsAsUse) {
    const auto &stmts = bodyOf(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            {
                println(r)
            }
            k = 42
        }
    )--");
    auto lu = findLastUse(stmts, 1, "r");
    EXPECT_TRUE(lu.shortenable);
    EXPECT_EQ(lu.stmtIndex, 2u);
}

TEST_F(BorrowLastUseTest, ClosureMentionBlocksShortening) {
    // Geri-çekilme kuralı 2: closure saklanıp son kullanımdan SONRA
    // çağrılabilir, dolayısıyla içindeki kullanım deyim sırasıyla sınırlı değil.
    const auto &stmts = bodyOf(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            let f = |x: i32| -> i32 { return x + r }
            k = 42
        }
    )--");
    auto lu = findLastUse(stmts, 1, "r");
    EXPECT_FALSE(lu.shortenable);
}

TEST_F(BorrowLastUseTest, ReborrowBlocksShortening) {
    // Geri-çekilme kuralı 4: `let s = ref r` sonrası r'nin ödüncünü bırakmak
    // s'in geçişli bağımlılığını görmezden gelirdi (Rust: E0506).
    const auto &stmts = bodyOf(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            let s = ref r
            k = 42
            println(s)
        }
    )--");
    auto lu = findLastUse(stmts, 1, "r");
    EXPECT_FALSE(lu.shortenable);
}

TEST_F(BorrowLastUseTest, UnexpandedMacroBlocksShortening) {
    // Geri-çekilme kuralı 3: makronun argüman token'ları henüz AST değil, adı
    // kullanıp kullanmadığı bilinemez. `name!(args)` ifade düzeyinde parse
    // ediliyor ve bu test Sema koşmadığı için genişletme YAPILMAMIŞ olur.
    const auto &stmts = bodyOf(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            dbg!(k)
            k = 42
        }
    )--");
    auto lu = findLastUse(stmts, 1, "r");
    EXPECT_FALSE(lu.shortenable);
}

TEST_F(BorrowLastUseTest, ShadowingNameExtendsConservatively) {
    // Ad-tabanlı tarama iç bloktaki AYRI `r`'yi de dış bağlamanın kullanımı
    // sayar. Bu yalnızca bırakmayı GECİKTİRİR — muhafazakâr yön.
    const auto &stmts = bodyOf(R"--(
        func main() {
            var k: i32 = 10
            var m: i32 = 20
            let r = ref k
            {
                let r = ref m
                println(r)
            }
        }
    )--");
    auto lu = findLastUse(stmts, 2, "r");
    EXPECT_TRUE(lu.shortenable);
    EXPECT_EQ(lu.stmtIndex, 3u) << "gölgeleyen kullanım bırakmayı geciktirmeli";
}

} // namespace
