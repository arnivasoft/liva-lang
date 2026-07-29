# Ödüncün Son Kullanımda Bırakılması — Uygulama Planı

> **Ajan çalışanlar için:** ZORUNLU ALT-SKILL: Bu planı görev-görev uygulamak için
> `superpowers:subagent-driven-development` (önerilen) veya
> `superpowers:executing-plans` kullanın. Adımlar takip için checkbox (`- [ ]`)
> sözdizimi kullanıyor.

**Hedef:** Bir `ref` bağlamasının tuttuğu ödünç, bağlamanın kapsamı bitene kadar
değil **son kullanımından** sonra bırakılsın; ek olarak `ref mut` ödünç canlıyken
doğrudan atamanın sessizce kabul edildiği delik kapatılsın.

**Mimari:** AST katmanına düğüm-çocuk tablosunu tek yerde toplayan
`forEachChild`/`walkSubtree` eklenir. Sema'ya bu tabloyu kullanan `findLastUse`
gelir: bir adın, bildirim deyiminden sonraki deyimler içindeki son geçtiği
indeksi bulur ve dört durumda kısaltmayı tamamen kapatır. `OwnershipChecker::visitBlockStmt`
deyimleri indeksle gezer ve son kullanımı içeren deyim bittiğinde tam o tek
ödüncü geri verir. Sağlamlık argümanı tek cümle: bırakma noktası bağlamanın
bildirildiği deyim listesindedir ve deyim listelerinin içinde geri kenar yoktur.

**Teknoloji:** C++20, LLVM 21 (Clang/clang-cl, `C:\LLVM`), CMake + Ninja,
GoogleTest.

**Spec:** `docs/superpowers/specs/2026-07-30-borrow-last-use-design.md`

## Global Kısıtlar

- **Dil standardı:** C++20. `-fno-exceptions` uyumlu kod yaz (MinGW yapısı bunu
  kullanıyor): `std::stoi` yerine `strtol`, exception fırlatan API kullanma.
- **Yapı komutu:** ilk kez `build_clang.bat` (Ninja, çıktı `build-clang/`),
  sonraki turlarda artımlı `cmake --build build-clang`.
- **Tek test koşumu:** `ctest --test-dir build-clang -R <test_adı> --output-on-failure`
- **Tam süit:** `ctest --test-dir build-clang --output-on-failure` — **`-j`
  KULLANMA.** Paralel koşum SelfHostTest/BuildCacheTest/IncrementalBenchmarkTest'te
  önceden var olan yarışları tetikliyor. Taban: **2732/2732**.
- **`-Werror`:** hedef-bazlı, `LIVA_WERROR=ON` ile açılıyor ve CI'da üç job'da
  açık. Uyarı bırakma.
- **`forEachChild`'ın `switch`'inde `default:` dalı OLMAYACAK.** `NodeKind`'ın 52
  değeri (13 Decl, 10 Stmt, 29 Expr) açıkça ele alınacak. Eksiksizlik garantisi
  `-Wswitch`; `default:` eklemek bu garantiyi yok eder.
- **Görev sırası zorunlu:** Görev 4 (`ref mut` deliği) Görev 3'ten (kısaltma)
  SONRA commit'lenmeli. Ters sırada, kısaltmanın meşru kılacağı desenler
  (`let r = ref mut k; r = 1; k = 5`) geçici olarak reddedilip mevcut testler
  kırılır.
- **Commit mesajları Türkçe**, Conventional Commits öneki (`feat(ast):`,
  `fix(sema):`, `docs(roadmap):`) ve şu iki satırla bitmeli:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
  ```

- **Dal:** `fix/borrow-last-use`. `main`'e commit YAPMA.

## Dosya Yapısı

| Dosya | Sorumluluk | Görev |
|---|---|---|
| `include/liva/AST/ASTWalk.h` (yeni) | `forEachChild`/`walkSubtree` bildirimleri | 1 |
| `src/AST/ASTWalk.cpp` (yeni) | 52 düğüm türü için çocuk tablosu; tek gezinti kaynağı | 1 |
| `tests/unit/ASTWalkTest.cpp` (yeni) | Gezintinin gerçekten her yere ulaştığının davranış testi | 1 |
| `CMakeLists.txt:51-57` (değişir) | `src/AST/ASTWalk.cpp` → `liva_ast` hedefi | 1 |
| `tests/CMakeLists.txt` (değişir) | `ast_walk_test` ve `borrow_last_use_test` kaydı | 1, 2 |
| `include/liva/Sema/BorrowLastUse.h` (yeni) | `LastUseResult` + `findLastUse` arayüzü | 2 |
| `src/Sema/BorrowLastUse.cpp` (yeni) | Son-kullanım taraması + dört geri-çekilme kuralı | 2 |
| `tests/unit/BorrowLastUseTest.cpp` (yeni) | `findLastUse`'un indeks ve `shortenable` birim testleri | 2 |
| `src/Sema/OwnershipChecker.cpp` (değişir) | `visitBlockStmt` bırakma noktası; `visitAssignExpr` deliği | 3, 4 |
| `tests/unit/OwnershipTest.cpp` (değişir) | Uçtan uca tanı pinleri | 3, 4 |
| `tests/unit/RuntimeExecTest.cpp` (değişir) | Hedef desenin gerçekten koşup 42 basması | 3 |
| `roadmap.md` (değişir) | Kayıt 134 (b) çözüldü + kalan kesinlik boşlukları | 5 |

---

### Görev 1: `forEachChild` / `walkSubtree` — AST katmanı

**Dosyalar:**
- Oluştur: `include/liva/AST/ASTWalk.h`
- Oluştur: `src/AST/ASTWalk.cpp`
- Test: `tests/unit/ASTWalkTest.cpp`
- Değiştir: `CMakeLists.txt:51-57` (`liva_ast` kaynak listesi)
- Değiştir: `tests/CMakeLists.txt` (yeni test hedefi)

**Arayüzler:**
- Consumes: yalnız AST başlıkları (`ASTNode.h`, `Decl.h`, `Stmt.h`, `Expr.h`).
  Sema'ya bağımlılık YOK.
- Produces:
  - `bool liva::forEachChild(const ASTNode *node, const std::function<void(const ASTNode *)> &fn)`
  - `bool liva::walkSubtree(const ASTNode *node, const std::function<void(const ASTNode *)> &fn)`
  - Her ikisi de `false` = "tanınmayan düğüm türü görüldü, çağıran muhafazakâr
    davranmalı". `walkSubtree` düğümün KENDİSİNİ de `fn`'e verir (pre-order).

- [ ] **Adım 1: Başarısız testi yaz**

`tests/unit/ASTWalkTest.cpp` oluştur:

```cpp
// ASTWalkTest: forEachChild/walkSubtree'nin AST'nin her köşesine gerçekten
// ulaştığını davranış üzerinden doğrular. switch'in eksiksizliği -Wswitch'e
// bırakıldığı için burada düğüm türü SAYMIYORUZ — ulaşılabilirliği ölçüyoruz.

#include "liva/AST/ASTWalk.h"
#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceManager.h"
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
        struct P { x: i32, y: i32 }
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
```

`tests/CMakeLists.txt`'e `ownership_test` kaydının hemen üstüne ekle:

```cmake
liva_add_test(ast_walk_test
    unit/ASTWalkTest.cpp
)
target_link_libraries(ast_walk_test PRIVATE liva_ast liva_parser)
```

- [ ] **Adım 2: Testin başarısız olduğunu doğrula**

```
cmake --build build-clang
```

Beklenen: **derleme hatası** — `liva/AST/ASTWalk.h: No such file or directory`.
(Test daha çalıştırılamıyor; başlık yok.)

- [ ] **Adım 3: Başlığı yaz**

`include/liva/AST/ASTWalk.h`:

```cpp
#pragma once

#include "liva/AST/ASTNode.h"

#include <functional>

namespace liva {

/// node'un DOĞRUDAN çocuklarını kaynak sırasıyla fn'e verir. Null çocuklar
/// (değersiz return, else'siz if) atlanır.
///
/// Dönüş: false = bu düğüm türü tabloda yok, çağıran muhafazakâr davranmalı.
/// switch'te `default:` dalı OLMADIĞI için yeni bir NodeKind eklendiğinde
/// -Wswitch derleme hatası verir; bu yüzden false dönüşü pratikte ulaşılamaz
/// ve yalnızca API sözleşmesi olarak duruyor. `default:` EKLEMEYİN — garantiyi
/// yok eder.
bool forEachChild(const ASTNode *node,
                  const std::function<void(const ASTNode *)> &fn);

/// node ve tüm alt ağacı üzerinde pre-order gezinti — node'un KENDİSİ de fn'e
/// verilir. Ziyaret edilen HERHANGİ bir düğümde tanınmama olursa false döner.
bool walkSubtree(const ASTNode *node,
                 const std::function<void(const ASTNode *)> &fn);

} // namespace liva
```

- [ ] **Adım 4: Çocuk tablosunu yaz**

`src/AST/ASTWalk.cpp`:

```cpp
#include "liva/AST/ASTWalk.h"

#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"

namespace liva {

namespace {

/// Null çocuk yaygın (değersiz return, else'siz if, gövdesiz protokol metodu),
/// bu yüzden her yayım buradan geçiyor.
inline void emit(const ASTNode *child,
                 const std::function<void(const ASTNode *)> &fn) {
    if (child)
        fn(child);
}

} // namespace

bool forEachChild(const ASTNode *node,
                  const std::function<void(const ASTNode *)> &fn) {
    if (!node)
        return true;

    using K = ASTNode::NodeKind;
    switch (node->getKind()) {
    // === Bildirimler ===
    case K::FuncDecl: {
        auto *d = static_cast<const FuncDecl *>(node);
        for (const auto &p : d->getParams())
            emit(p.defaultValue.get(), fn);
        emit(d->getBody(), fn);
        return true;
    }
    case K::VarDecl:
        emit(static_cast<const VarDecl *>(node)->getInit(), fn);
        return true;
    case K::StructDecl: {
        auto *d = static_cast<const StructDecl *>(node);
        for (const auto &f : d->getFields())
            emit(f.get(), fn);
        return true;
    }
    case K::FieldDecl: {
        auto *d = static_cast<const FieldDecl *>(node);
        emit(d->getGetter(), fn);
        emit(d->getSetter(), fn);
        emit(d->getWillSet(), fn);
        emit(d->getDidSet(), fn);
        emit(d->getLazyInit(), fn);
        return true;
    }
    case K::EnumDecl: {
        auto *d = static_cast<const EnumDecl *>(node);
        for (const auto &c : d->getCases())
            emit(c.get(), fn);
        return true;
    }
    // İlişkili tipler TypeRepr, ASTNode değil — AST çocuğu yok.
    case K::EnumCaseDecl:
        return true;
    case K::ImplDecl: {
        auto *d = static_cast<const ImplDecl *>(node);
        for (const auto &m : d->getMethods())
            emit(m.get(), fn);
        return true;
    }
    case K::ProtocolDecl: {
        auto *d = static_cast<const ProtocolDecl *>(node);
        for (const auto &m : d->getMethods())
            emit(m.get(), fn);
        return true;
    }
    case K::ImportDecl:
    case K::TypeAliasDecl:
    // MacroDecl gövdesini ham kaynak metni olarak tutuyor, AST olarak değil.
    case K::MacroDecl:
        return true;
    case K::ClassDecl: {
        auto *d = static_cast<const ClassDecl *>(node);
        for (const auto &m : d->getMembers()) {
            emit(m.field.get(), fn);
            emit(m.method.get(), fn);
        }
        return true;
    }
    case K::TestDecl:
        emit(static_cast<const TestDecl *>(node)->getBody(), fn);
        return true;

    // === Deyimler ===
    case K::ExprStmt:
        emit(static_cast<const ExprStmt *>(node)->getExpr(), fn);
        return true;
    case K::ReturnStmt:
        emit(static_cast<const ReturnStmt *>(node)->getValue(), fn);
        return true;
    case K::IfStmt: {
        auto *s = static_cast<const IfStmt *>(node);
        emit(s->getCondition(), fn);
        emit(s->getThenBody(), fn);
        emit(s->getElseBody(), fn);
        return true;
    }
    case K::WhileStmt: {
        auto *s = static_cast<const WhileStmt *>(node);
        emit(s->getCondition(), fn);
        emit(s->getBody(), fn);
        return true;
    }
    case K::ForStmt: {
        auto *s = static_cast<const ForStmt *>(node);
        emit(s->getIterable(), fn);
        emit(s->getBody(), fn);
        return true;
    }
    case K::BlockStmt: {
        auto *s = static_cast<const BlockStmt *>(node);
        for (const auto &st : s->getStatements())
            emit(st.get(), fn);
        return true;
    }
    case K::BreakStmt:
    case K::ContinueStmt:
        return true;
    case K::IfLetStmt: {
        auto *s = static_cast<const IfLetStmt *>(node);
        emit(s->getOptionalExpr(), fn);
        emit(s->getThenBody(), fn);
        emit(s->getElseBody(), fn);
        return true;
    }
    case K::WhileLetStmt: {
        auto *s = static_cast<const WhileLetStmt *>(node);
        emit(s->getOptionalExpr(), fn);
        emit(s->getBody(), fn);
        return true;
    }

    // === İfadeler ===
    case K::IntegerLiteralExpr:
    case K::FloatLiteralExpr:
    case K::BoolLiteralExpr:
    case K::StringLiteralExpr:
    case K::NilLiteralExpr:
    // Turbofish tip argümanları TypeRepr, ASTNode değil.
    case K::IdentifierExpr:
        return true;
    case K::BinaryExpr: {
        auto *e = static_cast<const BinaryExpr *>(node);
        emit(e->getLHS(), fn);
        emit(e->getRHS(), fn);
        return true;
    }
    case K::UnaryExpr:
        emit(static_cast<const UnaryExpr *>(node)->getOperand(), fn);
        return true;
    case K::CallExpr: {
        auto *e = static_cast<const CallExpr *>(node);
        emit(e->getCallee(), fn);
        for (const auto &a : e->getArgs())
            emit(a.get(), fn);
        return true;
    }
    case K::MemberExpr:
        emit(static_cast<const MemberExpr *>(node)->getObject(), fn);
        return true;
    case K::IndexExpr: {
        auto *e = static_cast<const IndexExpr *>(node);
        emit(e->getBase(), fn);
        emit(e->getIndex(), fn);
        return true;
    }
    case K::AssignExpr: {
        auto *e = static_cast<const AssignExpr *>(node);
        emit(e->getTarget(), fn);
        emit(e->getValue(), fn);
        return true;
    }
    case K::StructLiteralExpr: {
        auto *e = static_cast<const StructLiteralExpr *>(node);
        for (const auto &f : e->getFields())
            emit(f.value.get(), fn);
        return true;
    }
    // Pattern ayrı bir hiyerarşi (ASTNode DEĞİL) ve patternler ad BAĞLAR, ad
    // okumaz — atlamak bir kullanımı kaçırmaz.
    case K::MatchExpr: {
        auto *e = static_cast<const MatchExpr *>(node);
        emit(e->getSubject(), fn);
        for (const auto &arm : e->getArms()) {
            emit(arm.guard.get(), fn);
            emit(arm.body.get(), fn);
        }
        return true;
    }
    case K::ArrayLiteralExpr: {
        auto *e = static_cast<const ArrayLiteralExpr *>(node);
        for (const auto &el : e->getElements())
            emit(el.get(), fn);
        return true;
    }
    case K::TupleLiteralExpr: {
        auto *e = static_cast<const TupleLiteralExpr *>(node);
        for (const auto &el : e->getElements())
            emit(el.get(), fn);
        return true;
    }
    case K::CastExpr:
        emit(static_cast<const CastExpr *>(node)->getExpr(), fn);
        return true;
    case K::IsExpr:
        emit(static_cast<const IsExpr *>(node)->getExpr(), fn);
        return true;
    case K::RefExpr:
        emit(static_cast<const RefExpr *>(node)->getExpr(), fn);
        return true;
    case K::GroupExpr:
        emit(static_cast<const GroupExpr *>(node)->getExpr(), fn);
        return true;
    case K::RangeExpr: {
        auto *e = static_cast<const RangeExpr *>(node);
        emit(e->getStart(), fn);
        emit(e->getEnd(), fn);
        return true;
    }
    case K::UnwrapExpr:
        emit(static_cast<const UnwrapExpr *>(node)->getOperand(), fn);
        return true;
    case K::ClosureExpr:
        emit(static_cast<const ClosureExpr *>(node)->getBody(), fn);
        return true;
    case K::TryExpr:
        emit(static_cast<const TryExpr *>(node)->getOperand(), fn);
        return true;
    case K::TernaryExpr: {
        auto *e = static_cast<const TernaryExpr *>(node);
        emit(e->getCondition(), fn);
        emit(e->getThenExpr(), fn);
        emit(e->getElseExpr(), fn);
        return true;
    }
    case K::AwaitExpr:
        emit(static_cast<const AwaitExpr *>(node)->getOperand(), fn);
        return true;
    case K::YieldExpr:
        emit(static_cast<const YieldExpr *>(node)->getValue(), fn);
        return true;
    case K::ComptimeExpr:
        emit(static_cast<const ComptimeExpr *>(node)->getBody(), fn);
        return true;
    // GENİŞLETİLMEMİŞ bir makronun argüman token'ları henüz AST değil.
    // Eksiksizliğe ihtiyacı olan çağıranlar genişletilmemiş MacroInvokeExpr'i
    // opak saymalı (bkz. findLastUse geri-çekilme kuralı 3).
    case K::MacroInvokeExpr:
        emit(static_cast<const MacroInvokeExpr *>(node)->getExpanded(), fn);
        return true;
    }

    return false; // -Wswitch açıkken ulaşılamaz.
}

bool walkSubtree(const ASTNode *node,
                 const std::function<void(const ASTNode *)> &fn) {
    if (!node)
        return true;

    fn(node);

    bool complete = true;
    bool known = forEachChild(node, [&](const ASTNode *child) {
        if (!walkSubtree(child, fn))
            complete = false;
    });
    return known && complete;
}

} // namespace liva
```

`CMakeLists.txt`'te `liva_ast` kaynak listesine ekle (`src/AST/ASTPrinter.cpp`
satırının hemen ardına):

```cmake
    src/AST/ASTWalk.cpp
```

- [ ] **Adım 5: Testlerin geçtiğini doğrula**

```
cmake --build build-clang
ctest --test-dir build-clang -R ast_walk_test --output-on-failure
```

Beklenen: 8 testin tamamı PASS, sıfır derleyici uyarısı.

- [ ] **Adım 6: Commit**

```bash
git add include/liva/AST/ASTWalk.h src/AST/ASTWalk.cpp tests/unit/ASTWalkTest.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -F - <<'EOF'
feat(ast): genel forEachChild/walkSubtree gezinti tablosu

Gezinti mantığı bugün dört kopya halinde (ASTPrinter, LifetimeAnalysis::visitNode,
OwnershipChecker'ın kısmi ziyaretleri, ve her yeni analiz). Son-kullanım
analizinin eksiksiz bir gezintiye ihtiyacı var, o yüzden tabloyu tek yere
topluyoruz.

switch'te default: dalı YOK — NodeKind'ın 52 değeri açıkça ele alınıyor,
böylece yeni bir düğüm türü eklendiğinde -Wswitch derleme hatası veriyor.
Eksiksizlik garantisi çalışma zamanı bayrağı değil derleyici; false dönüşü
yalnızca API sözleşmesi olarak duruyor.

Pattern hiyerarşisi bilinçli olarak atlandı: Pattern bir ASTNode değil ve
patternler ad BAĞLAR, ad okumaz — bir patterni atlamak bir kullanımı kaçırmaz.
String interpolasyonu için özel iş gerekmedi, parser onu zaten
BinaryExpr+CallExpr(toString) biçimine şekerden arındırıyor.

8 ASTWalkTest: member/index zinciri, closure gövdesi, ternary/unary, struct
literali alan değerleri, string interpolasyonu, null çocuklar, while/for
gövdeleri, impl metot gövdeleri.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

### Görev 2: `findLastUse` — Sema

**Dosyalar:**
- Oluştur: `include/liva/Sema/BorrowLastUse.h`
- Oluştur: `src/Sema/BorrowLastUse.cpp`
- Test: `tests/unit/BorrowLastUseTest.cpp`
- Değiştir: `CMakeLists.txt` (`liva_sema` kaynak listesi, `src/Sema/OwnershipChecker.cpp` satırının yanı)
- Değiştir: `tests/CMakeLists.txt` (yeni test hedefi)

**Arayüzler:**
- Consumes: Görev 1'in `liva::forEachChild` / `liva::walkSubtree`'si.
- Produces:
  - `struct liva::LastUseResult { size_t stmtIndex; bool shortenable; }`
  - `liva::LastUseResult liva::findLastUse(const std::vector<std::unique_ptr<ASTNode>> &stmts, size_t declIndex, const std::string &name)`
  - Sözleşme: `stmtIndex` = ödüncün bırakılabileceği deyim indeksi. Aralıkta
    hiç kullanım yoksa `stmtIndex == declIndex`. `shortenable == false` iken
    `stmtIndex` anlamsızdır, çağıran hiç kısaltma yapmamalı.

- [ ] **Adım 1: Başarısız testi yaz**

`tests/unit/BorrowLastUseTest.cpp` oluştur:

```cpp
// BorrowLastUseTest: findLastUse'un deyim indeksi ve dört geri-çekilme
// kuralının birim testleri. Yalnız parse eder — Sema koşmaz, çünkü test edilen
// şey saf sözdizimsel bir tarama.

#include "liva/Sema/BorrowLastUse.h"

#include "liva/AST/Decl.h"
#include "liva/AST/Stmt.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceManager.h"
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
```

`tests/CMakeLists.txt`'e ekle:

```cmake
liva_add_test(borrow_last_use_test
    unit/BorrowLastUseTest.cpp
)
target_link_libraries(borrow_last_use_test PRIVATE liva_sema liva_parser)
```

- [ ] **Adım 2: Testin başarısız olduğunu doğrula**

```
cmake --build build-clang
```

Beklenen: **derleme hatası** — `liva/Sema/BorrowLastUse.h: No such file or directory`.

- [ ] **Adım 3: Başlığı yaz**

`include/liva/Sema/BorrowLastUse.h`:

```cpp
#pragma once

#include "liva/AST/ASTNode.h"

#include <memory>
#include <string>
#include <vector>

namespace liva {

struct LastUseResult {
    /// Ödüncün bırakılabileceği deyim indeksi. shortenable false iken
    /// anlamsızdır.
    size_t stmtIndex = 0;
    /// false → hiç kısaltma yapma; ödünç kapsam çıkışına kadar yaşamalı.
    bool shortenable = false;
};

/// `stmts` içinde `name` adının son geçtiği deyimi bulur.
///
/// Tarama aralığı: [declIndex + 1, stmts.size()) — bildirim deyiminden sonraki
/// her deyimin TÜM alt ağacı gezilir. Aralıkta hiç kullanım yoksa
/// stmtIndex == declIndex döner, yani ödünç bildirim deyimi bittiği anda
/// bırakılabilir.
///
/// Dört durumda shortenable = false döner (hepsi muhafazakâr ret yönünde):
///   1. walkSubtree tanınmayan bir düğüm bildirdi.
///   2. Ad bir ClosureExpr alt ağacında geçiyor — closure saklanıp sonra
///      çağrılabilir.
///   3. Aralıkta GENİŞLETİLMEMİŞ bir MacroInvokeExpr var — token'ları henüz
///      AST değil.
///   4. Ad bir RefExpr'in operandı olarak geçiyor (`let s = ref r`) — geçişli
///      yeniden ödünç.
LastUseResult findLastUse(const std::vector<std::unique_ptr<ASTNode>> &stmts,
                          size_t declIndex, const std::string &name);

} // namespace liva
```

- [ ] **Adım 4: Taramayı yaz**

`src/Sema/BorrowLastUse.cpp`:

```cpp
#include "liva/Sema/BorrowLastUse.h"

#include "liva/AST/ASTWalk.h"
#include "liva/AST/Expr.h"

namespace liva {

namespace {

struct ScanState {
    const std::string &name;
    bool used = false;    // ad bu deyimde geçiyor
    bool blocked = false; // bir geri-çekilme kuralı tetiklendi
};

/// Adın bir kullanımı mı, yoksa deyim sırasıyla sınırlanamayan bir erişim mi
/// olduğunu ayırır.
void inspect(const ASTNode *n, ScanState &st) {
    switch (n->getKind()) {
    case ASTNode::NodeKind::IdentifierExpr:
        if (static_cast<const IdentifierExpr *>(n)->getName() == st.name)
            st.used = true;
        return;

    case ASTNode::NodeKind::RefExpr: {
        // Kural 4: `let s = ref r` ödüncü zincirliyor; r'nin ödüncünü bırakmak
        // s üzerinden erişilebilirliği görmezden gelirdi.
        auto *re = static_cast<const RefExpr *>(n);
        const Expr *inner = re->getExpr();
        if (inner && inner->getKind() == ASTNode::NodeKind::IdentifierExpr &&
            static_cast<const IdentifierExpr *>(inner)->getName() == st.name)
            st.blocked = true;
        return;
    }

    case ASTNode::NodeKind::ClosureExpr: {
        // Kural 2: closure gövdesindeki kullanım deyim sırasıyla sınırlı değil.
        // İç tarama, bayrağın YALNIZ bu ad için tetiklenmesini sağlıyor.
        const std::string &target = st.name;
        bool found = false;
        walkSubtree(n, [&](const ASTNode *inner) {
            if (inner->getKind() == ASTNode::NodeKind::IdentifierExpr &&
                static_cast<const IdentifierExpr *>(inner)->getName() == target)
                found = true;
        });
        if (found)
            st.blocked = true;
        return;
    }

    case ASTNode::NodeKind::MacroInvokeExpr:
        // Kural 3: genişletilmemiş makronun token'ları AST değil.
        if (!static_cast<const MacroInvokeExpr *>(n)->getExpanded())
            st.blocked = true;
        return;

    default:
        return;
    }
}

} // namespace

LastUseResult findLastUse(const std::vector<std::unique_ptr<ASTNode>> &stmts,
                          size_t declIndex, const std::string &name) {
    LastUseResult result;
    // Aralıkta kullanım yoksa ödünç bildirim deyimi bittiği anda düşer.
    result.stmtIndex = declIndex;
    result.shortenable = true;

    for (size_t i = declIndex + 1; i < stmts.size(); ++i) {
        ScanState st{name};
        bool known = walkSubtree(stmts[i].get(),
                                 [&](const ASTNode *n) { inspect(n, st); });

        // Kural 1 ya da 2/3/4: tamamen vazgeç, kapsam çıkışı bırakır.
        if (!known || st.blocked) {
            result.shortenable = false;
            return result;
        }

        if (st.used)
            result.stmtIndex = i;
    }

    return result;
}

} // namespace liva
```

`CMakeLists.txt`'te `liva_sema` kaynak listesine, `src/Sema/OwnershipChecker.cpp`
satırının hemen ardına ekle:

```cmake
    src/Sema/BorrowLastUse.cpp
```

- [ ] **Adım 5: Testlerin geçtiğini doğrula**

```
cmake --build build-clang
ctest --test-dir build-clang -R borrow_last_use_test --output-on-failure
```

Beklenen: 8 testin tamamı PASS.

`UnexpandedMacroBlocksShortening` testi parse hatası verirse (makro sözdizimi
beklenenden farklıysa), `dbg!(k)` yerine `assert!(k)` dene; ikisi de parse
edilemezse testi `ExprStmt` içine elle kurulmuş bir `MacroInvokeExpr` ile
değiştir (`MacroInvokeExpr("dbg", {}, SourceRange::invalid())` — genişletilmemiş
olduğu için kural 3 tetiklenir) ve nedenini test yorumuna yaz.

- [ ] **Adım 6: Commit**

```bash
git add include/liva/Sema/BorrowLastUse.h src/Sema/BorrowLastUse.cpp tests/unit/BorrowLastUseTest.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -F - <<'EOF'
feat(sema): findLastUse — bir adın deyim listesindeki son kullanımı

Ödüncün son kullanımda bırakılabilmesi için gereken saf sözdizimsel tarama.
Bildirim deyiminden sonraki her deyimin tüm alt ağacı geziliyor (Görev 1'in
walkSubtree'si) ve adın son geçtiği deyim indeksi dönüyor; aralıkta hiç
kullanım yoksa bildirim deyiminin kendisi.

Dört geri-çekilme kuralı, hepsi muhafazakâr ret yönünde: tanınmayan düğüm,
ClosureExpr içinde kullanım (closure saklanıp sonra çağrılabilir),
genişletilmemiş MacroInvokeExpr (token'ları AST değil), ve RefExpr operandı
olarak kullanım. Sonuncusu geçişli yeniden ödüncü kapatıyor: `let s = ref r`
OwnershipChecker'da r üzerine ödünç kaydeder, dolayısıyla r'nin ödüncünü
bırakmak s'in bağımlılığını görmezden gelirdi (Rust: E0506).

Gölgeleme bilinçli olarak muhafazakâr: iç bloktaki ayrı bir `r` de kullanım
sayılır, bu yalnızca bırakmayı geciktirir.

8 BorrowLastUseTest.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

### Görev 3: Son-kullanım bırakma noktası — `OwnershipChecker`

**Dosyalar:**
- Değiştir: `src/Sema/OwnershipChecker.cpp:104-111` (`visitBlockStmt`)
- Test: `tests/unit/OwnershipTest.cpp` (dosya sonuna yeni pinler)
- Test: `tests/unit/RuntimeExecTest.cpp` (dosya sonuna koşum testi)

**Arayüzler:**
- Consumes: Görev 2'nin `liva::findLastUse` / `liva::LastUseResult`'ı;
  `OwnershipInfo`'nun mevcut `isRefBinding`, `borrowsName`, `borrowsMutable`
  alanları (`visitVarDecl` bunları zaten dolduruyor) ve mevcut
  `releaseBorrow(name, isMutable)` yardımcısı.
- Produces: davranış değişikliği. Yeni public API yok, `OwnershipChecker.h`
  DEĞİŞMİYOR — bekleyen bırakma listesi `visitBlockStmt`'in yerel değişkeni.

- [ ] **Adım 1: Başarısız testleri yaz**

`tests/unit/OwnershipTest.cpp` dosyasının SONUNA ekle:

```cpp
// === Son-kullanım kısaltması (roadmap 134 (b)) ===

TEST_F(OwnershipTest, BorrowReleasedAtLastUseOfBinding) {
    // Hedef desen. `r` bu noktadan sonra bir daha okunmuyor, dolayısıyla
    // ödüncün canlı kalması için gerekçe yok. Rust bunu kabul eder (NLL).
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            println(r)
            k = 42
            println(k)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, UnusedRefBindingReleasesImmediately) {
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            k = 42
            println(k)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, BorrowUsedInLoopReleasedAfterTheLoop) {
    // Döngü gövdesindeki kullanım, döngü DEYİMİNİ son kullanım yapar; bırakma
    // döngü tamamen bittikten sonra, bu yüzden geri kenar sorun değil.
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            for i in 0..3 {
                println(r)
            }
            k = 42
            println(k)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, WriteThroughRefThenMutateReferentAccepted) {
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            let r = ref mut k
            r = 1
            k = 5
            println(k)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(OwnershipTest, MutationInsideLoopWithLaterUseStillRejected) {
    // Kullanım ve mutasyon AYNI döngü gövdesinde: bir sonraki iterasyon
    // mutasyondan sonra r'yi okur, bu yüzden ret muhafazakâr DEĞİL, gerekli.
    // Son kullanım döngü deyiminin kendisi olduğundan bırakma döngüden sonra.
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            var n: i32 = 0
            while n < 3 {
                println(r)
                k = 42
                n = n + 1
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_move_while_borrowed));
}

TEST_F(OwnershipTest, ClosureCaptureBlocksShortening) {
    // Geri-çekilme kuralı 2: closure saklanıp sonra çağrılabilir, bu yüzden
    // kısaltma tamamen kapalı ve ödünç kapsam sonuna kadar yaşıyor.
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            let f = |x: i32| -> i32 { return x + r }
            k = 42
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_move_while_borrowed));
}

TEST_F(OwnershipTest, ReborrowBlocksShortening) {
    // Geri-çekilme kuralı 4: `s` geçişli olarak k'ya erişiyor, dolayısıyla
    // r'nin ödüncü bırakılamaz. Rust: E0506.
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            let r = ref k
            let s = ref r
            k = 42
            println(s)
        }
    )--");
    EXPECT_FALSE(result.passed);
}

TEST_F(OwnershipTest, ShadowedBindingExtendsBorrowConservatively) {
    // Ad-tabanlı tarama iç bloktaki AYRI `r`'yi de kullanım sayıyor, bu yüzden
    // aradaki mutasyon reddediliyor. Rust bunu kabul eder; kesinlik boşluğu
    // bilinçli ve roadmap'e yazılı.
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            var m: i32 = 20
            let r = ref k
            k = 42
            {
                let r = ref m
                println(r)
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
}
```

`tests/unit/RuntimeExecTest.cpp` dosyasının SONUNA (son `TEST(...)` bloğundan
sonra, `#endif // LIVA_HAS_LLVM` satırından ÖNCE) ekle:

```cpp
TEST(RuntimeExecTest, BorrowReleasedAtLastUse_MutatesAfterwards) {
    // roadmap 134 (b): ödünç son kullanımda düşüyor, dolayısıyla `k = 42`
    // hem Sema'dan geçiyor hem doğru değeri basıyor.
    auto r = compileAndRun(R"(
        func main() {
            var k: i32 = 10
            let r = ref k
            println(r)
            k = 42
            println(k)
        }
    )", "borrow_last_use");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("42"), std::string::npos)
        << "stdout: " << r.stdout_output;
}
```

- [ ] **Adım 2: Testlerin başarısız olduğunu doğrula**

```
cmake --build build-clang
ctest --test-dir build-clang -R "ownership_test|runtime_exec" --output-on-failure
```

Beklenen: `BorrowReleasedAtLastUseOfBinding`, `UnusedRefBindingReleasesImmediately`,
`BorrowUsedInLoopReleasedAfterTheLoop`, `WriteThroughRefThenMutateReferentAccepted`
ve `BorrowReleasedAtLastUse_MutatesAfterwards` FAIL (`cannot move 'k' while it
is borrowed`). `MutationInsideLoopWithLaterUseStillRejected`,
`ClosureCaptureBlocksShortening`, `ReborrowBlocksShortening`,
`ShadowedBindingExtendsBorrowConservatively` şimdiden PASS — bugün her şey
reddedildiği için. Bunlar Adım 4'ten sonra da PASS kalmalı; asıl değerleri o.

- [ ] **Adım 3: Bırakma noktasını uygula**

`src/Sema/OwnershipChecker.cpp` dosyasının başındaki include'lara ekle:

```cpp
#include "liva/Sema/BorrowLastUse.h"
```

`visitBlockStmt`'i (satır 104-111) tamamen değiştir:

```cpp
// Bir `ref` bağlamasının tuttuğu ödünç, bağlamanın SON KULLANIMINDAN sonra
// bırakılıyor — kapsam çıkışını beklemiyor (roadmap 134 (b)).
//
// Neden sağlam: bırakma noktası bağlamanın bildirildiği deyim listesinde ve
// bildirimden sonra. Deyim listeleri sırasaldır ve içlerinde geri kenar yoktur
// — döngüler tek bir deyimdir ve biz bir döngü deyimini TAMAMEN bittikten
// sonra bırakırız. Ad-tabanlı taramanın fazla saydığı durumlar (gölgeleme,
// dallar) bırakmayı yalnızca GECİKTİRİR, asla öne almaz.
//
// Erken çıkışlar (return/break/continue) bu noktayı atlar; kapsam çıkışındaki
// dropScopeVariables bırakması yedek olarak yerinde duruyor.
void OwnershipChecker::visitBlockStmt(BlockStmt *node) {
    pushOwnershipScope();

    const auto &stmts = node->getStatements();
    // {bırakmanın yapılacağı deyim indeksi, bağlama adı}
    std::vector<std::pair<size_t, std::string>> pendingRelease;

    for (size_t i = 0; i < stmts.size(); ++i) {
        visit(stmts[i].get());

        // Bu deyim bir `ref` bağlaması ürettiyse, ödüncünü ne zaman geri
        // vereceğini şimdi hesapla. borrowsName boşsa (referent izlenmiyor:
        // global, alan, ya da reddedilmiş ödünç) bırakılacak bir şey yok.
        if (stmts[i]->getKind() == ASTNode::NodeKind::VarDecl) {
            auto *varDecl = static_cast<VarDecl *>(stmts[i].get());
            auto *info = getInfo(varDecl->getName());
            if (info && info->isRefBinding && !info->borrowsName.empty()) {
                LastUseResult lastUse =
                    findLastUse(stmts, i, varDecl->getName());
                if (lastUse.shortenable)
                    pendingRelease.emplace_back(lastUse.stmtIndex,
                                                varDecl->getName());
            }
        }

        // Bu deyimde ölen bağlamaların ödüncünü geri ver. Kullanımı olmayan
        // bir bağlama için indeks bildirim deyiminin kendisidir, o yüzden
        // kayıt aynı turda hem eklenip hem işlenebilir.
        for (auto it = pendingRelease.begin(); it != pendingRelease.end();) {
            if (it->first != i) {
                ++it;
                continue;
            }
            if (auto *bindingInfo = getInfo(it->second)) {
                releaseBorrow(bindingInfo->borrowsName,
                              bindingInfo->borrowsMutable);
                // ZORUNLU: kapsam çıkışının aynı ödüncü ikinci kez
                // düşürmesini, ve referent sonradan BAŞKA bir ödünç aldıysa
                // kapsam çıkışının o yeni ödüncü silmesini engelliyor.
                bindingInfo->borrowsName.clear();
            }
            it = pendingRelease.erase(it);
        }
    }

    dropScopeVariables();
    popOwnershipScope();
}
```

- [ ] **Adım 4: Testlerin geçtiğini doğrula**

```
cmake --build build-clang
ctest --test-dir build-clang -R "ownership_test|borrow_last_use_test|ast_walk_test" --output-on-failure
```

Beklenen: 105 mevcut + 8 yeni OwnershipTest'in TAMAMI PASS. Özellikle şu üç
mevcut pin değişmeden geçmeli — kullanımı mutasyondan sonraya koydukları için
kısaltmadan etkilenmemeleri gerekiyor:
`BorrowStillLiveInSameScopeStillBlocksMutation`,
`BindingBorrowStillBlocksLaterArgBorrow`,
`ArgBorrowReleaseDoesNotClearBindingBorrow`.

`ClosureCaptureBlocksShortening` beklenen tanı yerine BAŞKA bir hatayla
başarısız olursa (`hasDiag(err_move_while_borrowed)` false), sorun kısaltmada
değil olabilir: Liva closure'ları dış yerelleri yakalayamıyorsa Sema zaten farklı
bir tanı üretir. Bu durumda testi, aynı geri-çekilme kuralını closure'un yalnız
PARSE edilmesiyle ölçen Görev 2'nin `ClosureMentionBlocksShortening` birim testine
bırak, uçtan uca pini kaldır ve nedenini test dosyasına yorum olarak yaz.

Sonra koşum testi:

```
ctest --test-dir build-clang -R runtime_exec --output-on-failure
```

Beklenen: `BorrowReleasedAtLastUse_MutatesAfterwards` PASS, stdout'ta `42`.

- [ ] **Adım 5: Tam süiti koş**

```
ctest --test-dir build-clang --output-on-failure
```

**`-j` KULLANMA.** Beklenen: 2732 + yeni testler, sıfır regresyon. Regresyon
çıkarsa: düşen testin kullanımı mutasyondan ÖNCE mi sonra mı olduğuna bak —
öncesindeyse kısaltma doğru çalışıyor ve test eski (kapsam-çıkışı) davranışı
pinliyordu, bu durumda testi ve gerekçesini güncelle; sonrasındaysa bırakma
noktası fazla erken, kök nedeni bul.

- [ ] **Adım 6: Commit**

```bash
git add src/Sema/OwnershipChecker.cpp tests/unit/OwnershipTest.cpp tests/unit/RuntimeExecTest.cpp
git commit -F - <<'EOF'
fix(sema): bir ref bağlamasının ödüncü son kullanımından sonra bırakılıyor

roadmap 134 (b). Önceki commit ödüncü kapsam ÇIKIŞINDA bırakmayı düzeltmişti;
bu, kalan yarısı: bırakma artık son kullanımdan sonra oluyor, dolayısıyla

  var k: i32 = 10
  let r = ref k
  println(r)
  k = 42        // önceden: cannot move 'k' while it is borrowed

derleniyor ve 42 basıyor. Rust bunu NLL ile kabul ediyor.

Mekanizma: visitBlockStmt deyimleri indeksle geziyor; bir ref bağlaması
gördüğünde findLastUse ile bırakma indeksini hesaplıyor ve o deyim bittiğinde
tam o tek ödüncü geri veriyor. Sonrasında borrowsName temizleniyor — bu
zorunlu, yoksa kapsam çıkışı ya aynı ödüncü ikinci kez düşürür ya da
referentin sonradan aldığı BAŞKA bir ödüncü siler.

Neden sağlam: bırakma noktası bağlamanın bildirildiği deyim listesinde ve
bildirimden sonra. Deyim listeleri sırasaldır ve içlerinde geri kenar yoktur —
döngüler tek bir deyimdir ve döngü deyimi tamamen bittikten sonra bırakılır.
Ad-tabanlı taramanın fazla saydığı durumlar bırakmayı yalnızca geciktirir.

Yeni pinler kabul tarafında: hedef desen, hiç kullanılmayan bağlama, döngüden
sonra mutasyon, ref üzerinden yazma sonrası mutasyon. Ret tarafında: aynı
döngü gövdesinde kullanım+mutasyon (gerekli ret, muhafazakâr değil), closure
yakalama, geçişli yeniden ödünç, gölgeleme. Mevcut üç pin
(BorrowStillLiveInSameScope…, BindingBorrowStillBlocks…,
ArgBorrowReleaseDoesNotClear…) kullanımı mutasyondan sonraya koyduğu için
değişmeden geçiyor.

8 OwnershipTest + 1 RuntimeExecTest.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

### Görev 4: `ref mut` ödünç canlıyken atama deliği

**ÖNKOŞUL:** Görev 3 commit'lenmiş olmalı. Bu görev tek başına inerse
`WriteThroughRefThenMutateReferentAccepted` gibi desenler reddedilir.

**Dosyalar:**
- Değiştir: `src/Sema/OwnershipChecker.cpp:259-263` (`visitAssignExpr`)
- Test: `tests/unit/OwnershipTest.cpp`

**Arayüzler:**
- Consumes: mevcut `OwnershipState::BorrowedMutable` durumu ve
  `DiagID::err_move_while_borrowed`.
- Produces: davranış değişikliği. Yeni DiagID YOK.

- [ ] **Adım 1: Başarısız testi yaz**

`tests/unit/OwnershipTest.cpp` sonuna ekle:

```cpp
TEST_F(OwnershipTest, AssignWhileMutablyBorrowedRejected) {
    // visitAssignExpr yalnız BorrowedImmutable'ı reddediyordu, bu yüzden
    // DEĞİŞEBİLİR ödünç canlıyken doğrudan atama sessizce kabul ediliyordu.
    // Rust: E0506. Kullanım mutasyondan SONRA olduğu için son-kullanım
    // kısaltması bu ödüncü bırakmıyor.
    auto result = check(R"--(
        func main() {
            var k: i32 = 10
            let r = ref mut k
            k = 42
            println(r)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_move_while_borrowed));
}
```

- [ ] **Adım 2: Testin başarısız olduğunu doğrula**

```
cmake --build build-clang
ctest --test-dir build-clang -R ownership_test --output-on-failure
```

Beklenen: `AssignWhileMutablyBorrowedRejected` FAIL — `result.passed` true
geliyor, hiç tanı üretilmiyor.

- [ ] **Adım 3: Koşulu genişlet**

`src/Sema/OwnershipChecker.cpp`, `visitAssignExpr` içindeki blok:

```cpp
        // If assigning a non-copy value, it's a move
        auto *info = getInfo(ident->getName());
        if (info && info->state == OwnershipState::BorrowedImmutable) {
            diag_.report(node->getStartLoc(), DiagID::err_move_while_borrowed,
                         ident->getName());
        }
```

şununla değiştir:

```cpp
        // Ödünçlü bir değişkene doğrudan atama, ödüncün türünden bağımsız
        // olarak reddedilir. Yalnız BorrowedImmutable'a bakmak DEĞİŞEBİLİR
        // ödüncü sessizce geçiriyordu: `let r = ref mut k` canlıyken `k = 42`
        // hiç tanı üretmiyordu (Rust: E0506).
        //
        // Referente YAZMA (`r = 99`) bu kontrole girmiyor: hedef `r`'dir ve
        // ödünç `k` üzerinde kayıtlı, dolayısıyla r'nin kendi durumu Owned.
        auto *info = getInfo(ident->getName());
        if (info && (info->state == OwnershipState::BorrowedImmutable ||
                     info->state == OwnershipState::BorrowedMutable)) {
            diag_.report(node->getStartLoc(), DiagID::err_move_while_borrowed,
                         ident->getName());
        }
```

- [ ] **Adım 4: Testin geçtiğini doğrula**

```
cmake --build build-clang
ctest --test-dir build-clang -R ownership_test --output-on-failure
```

Beklenen: yeni test PASS, önceki 113 testin tamamı PASS.

- [ ] **Adım 5: Tam süiti koş**

```
ctest --test-dir build-clang --output-on-failure
```

**`-j` KULLANMA.** Bu görev yeni RET üretebildiği için gerçek risk burada: bir
test ya da stdlib dosyası `ref mut` ödünç canlıyken referente yazıyorsa yüzeye
çıkar. Düşen olursa, önce desenin gerçekten sağlamsız olup olmadığına bak
(kullanım mutasyondan sonra mı?) — sağlamsızsa test yanlıştı, düzelt ve
gerekçeyi yaz; değilse (kullanım mutasyondan önce ve kısaltma tetiklenmeliydi)
Görev 3'ün bırakma noktasında bir boşluk var, orayı araştır.

- [ ] **Adım 6: Commit**

```bash
git add src/Sema/OwnershipChecker.cpp tests/unit/OwnershipTest.cpp
git commit -F - <<'EOF'
fix(sema): değişebilir ödünç canlıyken referente atama reddediliyor

visitAssignExpr yalnız BorrowedImmutable durumunu reddediyordu, bu yüzden

  var k: i32 = 10
  let r = ref mut k
  k = 42            // hiç tanı YOK
  println(r)

sessizce kabul ediliyordu. Rust bunu E0506 ile reddeder: r canlıyken k'ya
yazmak ödüncün gördüğü değeri altından çekiyor.

Koşula BorrowedMutable eklendi, yeni DiagID yok — err_move_while_borrowed
atama yolunda zaten kullanılan tanı. Referente YAZMA (`r = 99`) etkilenmiyor:
hedef `r`'dir ve ödünç `k` üzerinde kayıtlı, dolayısıyla r'nin kendi durumu
Owned ve bu kontrole hiç girmiyor.

Bu commit son-kullanım kısaltmasından SONRA gelmek zorundaydı: tek başına
inerse kısaltmanın meşru kıldığı desenleri (let r = ref mut k; r = 1; k = 5)
reddedip mevcut testleri kırardı.

1 OwnershipTest.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

### Görev 5: Kapanış — roadmap, örnek ölçümü, tam süit raporu

**Dosyalar:**
- Değiştir: `roadmap.md:134` (kayıt gövdesi)

**Arayüzler:**
- Consumes: Görev 3 ve 4'ün ölçülmüş sonuçları.
- Produces: yok (dokümantasyon).

- [ ] **Adım 1: Derlenmeyen örnekleri ölç**

`roadmap.md:117` `ownership` ve `ownership_demo` örneklerini "borrow checker"
gerekçesiyle derlenmeyenler arasında sayıyor. Bu iş onları açmış olabilir:

```
./build-clang/livac.exe examples/ownership.liva -o build-clang/_ex_ownership.exe
./build-clang/livac.exe examples/ownership_demo.liva -o build-clang/_ex_ownership_demo.exe
```

(Örneklerin tam yolunu `ls examples/ | grep ownership` ile doğrula.) Çıktıyı
not al — hâlâ başarısızsa hata mesajını kaydet, artık derleniyorsa bunu
roadmap'e yaz. **ExamplesTest kapısına ekleme bu görevin kapsamında değil**;
ayrı bir iş olarak roadmap'e yazılacak.

- [ ] **Adım 2: Tam süiti son kez koş ve sayıyı kaydet**

```
ctest --test-dir build-clang --output-on-failure
```

**`-j` KULLANMA.** Geçen test sayısını not al (taban 2732 + bu planın
eklediği 25 test = beklenen 2757, ± mevcut opt-in skip'ler:
PgRealRoundTrip, HttpLiveRoundTrip, WsLiveEchoRoundTrip).

- [ ] **Adım 3: roadmap kaydını güncelle**

`roadmap.md`'de kayıt 134'ün sonundaki şu metni bul:

```
KALAN, iki parça: (a) bir BAĞLAMANIN tuttuğu ödünç KAPSAM SONUNDA bile bırakılmıyordu — çözüldü 2026-07, aşağıdaki ayrı kayda bakın; (b) ödünç hâlâ ifade-düzeyinde değil: bağlamanın ödüncü SON KULLANIMINDA değil kapsam sonunda düşüyor (muhafazakâr ve sağlam, ama `let r = ref k; println(r); k = 42` hâlâ reddediliyor — gerçek NLL/liveness analizi ister, döngü ve dal içindeki kullanımlar yüzünden basit "son satır" kuralı güvenli değil)
```

ve şununla değiştir (ölçülen test sayısını `<N>` yerine yaz):

```
KALAN yok — iki parça da çözüldü: (a) bir BAĞLAMANIN tuttuğu ödünç KAPSAM SONUNDA bırakılıyor (2026-07, ayrı kayıt); (b) ödünç SON KULLANIMDA bırakılıyor (2026-07-30). (b)'nin çözümü: `forEachChild`/`walkSubtree` (AST katmanında 52 düğüm için tek çocuk-tablosu, `switch`'te `default:` YOK — eksiksizlik garantisi `-Wswitch`), Sema'da `findLastUse` (bildirim deyiminden sonraki deyimlerde adın son geçtiği indeks), ve `visitBlockStmt`'te deyim-granülerliğinde bırakma noktası + `borrowsName` temizliği. Sağlamlık argümanı: bırakma noktası bağlamanın bildirildiği deyim listesindedir ve deyim listelerinin içinde geri kenar yoktur (döngüler tek deyimdir, döngü deyimi TAMAMEN bittikten sonra bırakılır); ad-tabanlı taramanın fazla saydığı durumlar bırakmayı yalnız geciktirir. Dört geri-çekilme kuralı, hepsi muhafazakâr ret yönünde: tanınmayan düğüm, ClosureExpr içinde kullanım, genişletilmemiş MacroInvokeExpr, ve RefExpr operandı olarak kullanım (geçişli yeniden ödünç — bu kural olmadan `let s = ref r; k = 42; println(s)` sağlamsız biçimde kabul edilirdi, Rust: E0506). Aynı işte kapatılan komşu delik: `visitAssignExpr` yalnız `BorrowedImmutable`'ı reddediyordu, `let r = ref mut k` canlıyken `k = 42` sessizce kabul ediliyordu. | `src/AST/ASTWalk.cpp` (yeni), `src/Sema/BorrowLastUse.cpp` (yeni), `src/Sema/OwnershipChecker.cpp` `visitBlockStmt`/`visitAssignExpr`; 8 ASTWalkTest + 8 BorrowLastUseTest + 9 OwnershipTest + 1 RuntimeExecTest, tam süit <N>/<N>. KALAN KESİNLİK BOŞLUKLARI (üçü de yol duyarlılığı, yani gerçek CFG + liveness dataflow istiyor — 2.5 #2 ile aynı iş): dal-ayrımlı ölüm (`if c { println(r) } else { k = 42 }` Rust'ta kabul, bizde ret), closure sonrası ölüm, ve gölgeleme (iç kapsamdaki aynı adlı ayrı bağlama dış ödüncü gereksiz uzatıyor). AYRI İŞ: OwnershipChecker'ın gezinti boşlukları — `MemberExpr`/`IndexExpr`/`UnaryExpr`/`ClosureExpr`/`MatchExpr`/`TernaryExpr` hiç gezilmiyor, dolayısıyla bu yollardan geçen kullanım-sonrası-taşıma denetlenmiyor; `forEachChild` bunu kapatmanın altyapısını veriyor
```

Ayrıca Adım 1'de örnekler artık derleniyorsa, `roadmap.md:117`'deki kayıtta
`ownership`/`ownership_demo` maddesini "borrow checker (çözüldü 2026-07-30,
ExamplesTest kapısına eklenmesi ayrı iş)" olarak güncelle.

- [ ] **Adım 4: Commit**

```bash
git add roadmap.md
git commit -F - <<'EOF'
docs(roadmap): ödünç son kullanımda bırakılıyor — kayıt 134 kapandı

(b) parçası çözüldü: mekanizma, sağlamlık argümanı ve dört geri-çekilme kuralı
kayda işlendi. Kalan kesinlik boşlukları (dal-ayrımlı ölüm, closure sonrası
ölüm, gölgeleme) tek yerde toplandı — üçü de yol duyarlılığı istiyor, yani
2.5 #2'deki gerçek CFG + liveness işi.

Ayrı iş olarak not düşüldü: OwnershipChecker MemberExpr/IndexExpr/UnaryExpr/
ClosureExpr/MatchExpr/TernaryExpr düğümlerini hiç gezmiyor, bu yüzden o
yollardan geçen kullanım-sonrası-taşıma denetlenmiyor; bu işte eklenen
forEachChild bunu kapatmanın altyapısı.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVTT1GUV7ukJL9zRqur7oz
EOF
```

---

## Kabul Ölçütleri (spec'ten)

1. ✅ Hedef desen (`let r = ref k; println(r); k = 42`) hem Sema'dan geçiyor hem
   doğru değeri basıyor → Görev 3, `BorrowReleasedAtLastUseOfBinding` +
   `BorrowReleasedAtLastUse_MutatesAfterwards`.
2. ✅ Dört geri-çekilme kuralının her biri pin testiyle sabitlenmiş → Görev 2
   (birim: `ClosureMentionBlocksShortening`, `ReborrowBlocksShortening`,
   `UnexpandedMacroBlocksShortening`; kural 1 `-Wswitch` ile derleme zamanında)
   + Görev 3 (uçtan uca: `ClosureCaptureBlocksShortening`,
   `ReborrowBlocksShortening`).
3. ✅ `ref mut` atama deliği kapalı ve pinli → Görev 4.
4. ✅ Mevcut 105 OwnershipTest geçiyor, tam süitte sıfır regresyon → Görev 3
   Adım 5, Görev 4 Adım 5, Görev 5 Adım 2.
5. ✅ `forEachChild`'ın `switch`'inde `default:` yok → Görev 1 Adım 4, global
   kısıtlarda tekrarlanıyor.
6. ✅ `roadmap.md:134` güncellendi → Görev 5.

## Notlar

- **Kural 1'in (tanınmayan düğüm) çalışma zamanı testi yok**, çünkü `-Wswitch`
  + `default:` yokluğu onu derleme zamanında imkânsız kılıyor. `walkSubtree`'nin
  `false` dönüşü ASTWalkTest'te `EXPECT_TRUE(complete)` ile ters yönden
  pinleniyor: bugün hiçbir düğüm tanınmıyor durumuna düşmüyor.
- **`OwnershipChecker.h` bu planda hiç değişmiyor.** Bekleyen bırakma listesi
  `visitBlockStmt`'in yerel değişkeni; üye alan eklemek iç içe blokları
  karıştırırdı.
- **`findLastUse` Sema'da, gezinti AST'de.** Bu sınır bilinçli: gezinti tablosu
  dil yapısına ait ve başka analizlerin de işine yarayacak; son-kullanım kuralı
  ödünç semantiğine ait ve yalnız Sema'yı ilgilendiriyor.
- **`allVariables_` ad-anahtarlı ve gölgelemeyi düzgün taşımıyor** (bu plandan
  ÖNCE var olan bir kusur, kapsam dışı). İç bir kapsam aynı adı bildirip
  çıktığında `dropScopeVariables` o adı flat haritadan siliyor, yani dış
  bağlama artık `getInfo` ile bulunamıyor. Görev 3'ün bırakma döngüsü bu yüzden
  `if (auto *bindingInfo = getInfo(...))` ile korunuyor: bulunamazsa hiç bırakma
  yapılmıyor ve ödünç, dış kapsamın `dropScopeVariables`'ı tarafından (flat
  haritayı değil kendi scope map'ini gezdiği için) yine de geri veriliyor.
  `ShadowedBindingExtendsBorrowConservatively` testi tam bu yoldan geçiyor —
  çökmemesi ve reddi koruması bekleniyor.
- Aynı blokta bir adın İKİ kez `ref` bağlanması (`let r = ref k` … `let r = ref m`)
  durumunda bırakma, o an `getInfo`'nun döndürdüğü — yani en son bildirilen —
  bağlamanın ödüncünü düşürür. Var olan bir ödüncü bıraktığı için sağlamsız
  değil, yalnız kesinlik kaybı; ayrıca bu desende kural 4 (RefExpr operandı)
  ya da gölgeleme fazla-sayımı çoğu zaman kısaltmayı zaten kapatıyor.
