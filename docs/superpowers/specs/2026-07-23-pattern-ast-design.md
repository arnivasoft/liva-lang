# Yapısal Pattern Matching — Faz A: Pattern AST Altyapısı (Tasarım)

Tarih: 2026-07-23
Kapsam: Roadmap #3'ün altyapı yarısı. `MatchArm::pattern` düz string'inin yapısal `Pattern` AST'siyle değiştirilmesi ve 7 tüketicinin (Parser, TypeChecker, IRGenCall, IRGenExpr, DAPInterpreter, ASTPrinter, MacroExpander) dönüştürülmesi. **Davranış-koruyucu**: bugün çalışan tüm match semantiği birebir korunur; yeni pattern türü EKLENMEZ (Faz B ayrı spec).
Onay: Kullanıcı A+B fazlarını birlikte onayladı (2026-07-23); B, A merge edildikten sonra ayrı döngüde.

## Mevcut durum

- `MatchArm` (`include/liva/AST/Expr.h:337`): `std::string pattern` — parser token metinlerini boşluksuz birleştiriyor (`ParseExpr.cpp:731-739`).
- IRGen string'i ikinci kez parse ediyor: `parseMatchPattern` → `PatternInfo` (`IRGenCall.cpp:1629+`), iç içe enum desteğiyle (`emitNestedPatternMatch`).
- Bare identifier (case mi binding mi) belirsizliği IRGen'de `subjectEnumType` ile çözülüyor.
- Bugün çalışan semantik: `_` wildcard; `Enum.Case` / bare `Case`; `Enum.Case(b1, b2)` bindingler + iç içe enum pattern'leri; int literal; subject-binding (`s if s >= 90`); guard'lar (`if`/`where`).

## Hedef mimari

**Yeni `include/liva/AST/Pattern.h`:**

```cpp
class Pattern {  // ASTNode'dan bağımsız hafif hiyerarşi (SourceRange taşır)
public:
    enum class Kind { Wildcard, Identifier, EnumCase, IntLiteral };
    Kind getKind() const; SourceRange getRange() const;
    std::string toString() const;  // parser'ın bugünkü boşluksuz birleştirmesiyle BAYT-ÖZDEŞ
    virtual ~Pattern();
};
class WildcardPattern : Pattern {};                    // _
class IdentifierPattern : Pattern { std::string name; };  // bare ad; case-vs-binding çözümü tüketicide (mevcut kural)
class EnumCasePattern : Pattern {
    std::string enumName;   // boş olabilir (bare Case)
    std::string caseName;
    std::vector<std::unique_ptr<Pattern>> subpatterns;  // iç içe
};
class IntLiteralPattern : Pattern { int64_t value; std::string text; };  // text: orijinal yazım (toString için)
```

**Geçiş stratejisi — çift alan, tüketici-tüketici dönüşüm:**
1. `MatchArm`'a `std::unique_ptr<Pattern> patternNode` EKLENİR; mevcut `pattern` string'i bir süre KALIR ve parser onu bugünkü mekanizmayla (token birleştirme) üretmeye devam eder. Böylece hiçbir tüketici bozulmadan Pattern AST'si devreye girer.
2. Tüketiciler tek tek `patternNode`'a dönüştürülür (her dönüşüm ayrı task + tam test döngüsü): IRGen (string-reparse katmanı `parseMatchPattern`/`PatternInfo` silinir, `emitNestedPatternMatch` Pattern alır) → TypeChecker (exhaustiveness dahil) → DAPInterpreter → ASTPrinter + MacroExpander.
3. Son task: `MatchArm::pattern` string alanı ve ona yazan parser kodu SİLİNİR; string'e dokunan hiçbir tüketici kalmadığı grep'le kanıtlanır.

**Parser:** `parseMatchExpr` gerçek pattern grameri kurar (recursive): `_` → Wildcard; int literal → IntLiteral; `Ident` → Identifier; `Ident.Ident[(alt-pattern'ler)]` veya `Ident(alt-pattern'ler)` → EnumCase (alt-pattern'ler recursive). Bugün parse EDİLEBİLEN her pattern bu grameri karşılar; gramerin dışına düşen token dizisi görülürse mevcut davranışı bozmamak için parser aynı token-birleştirme fallback'ini kullanamaz (string alanı silinince) — Task 0 keşfi bu riski haritalar; testlerde/örneklerde gramer-dışı pattern bulunursa DURDUR ve raporla.

## Doğrulama

- Her task sonunda tam seri suite 2340/2340 (match/enum testleri dahil).
- Task 1 kapısı: parser hem string hem Pattern üretirken `patternNode->toString() == pattern` eşitliği TÜM testler boyunca runtime-assert ile doğrulanır (yalnızca geçiş süresince duran bir kontrol; son task'ta stringle birlikte kalkar).
- Davranış değişikliği yok; yeni sözdizimi yok; yeni diagnostik yok (mevcut hatalı-pattern davranışı neyse o kalır).

## Kapsam dışı (Faz B — ayrı spec)

Or-pattern, range pattern, string/float/bool literal, `@` binding, tuple destructuring, exhaustiveness genişletmesi, LSP/tree-sitter/doc güncellemeleri.
