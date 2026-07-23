# Pattern AST Altyapısı (Faz A) — Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `MatchArm::pattern` string'i yapısal `Pattern` AST'siyle değiştirilir; 7 tüketici dönüştürülür; string-reparse katmanı silinir. Davranış birebir korunur.

**Architecture:** Spec: `docs/superpowers/specs/2026-07-23-pattern-ast-design.md`. Çift-alan geçişi: önce `patternNode` string'in YANINA eklenir (toString eşitliği runtime-assert'le kanıtlı), tüketiciler tek tek dönüştürülür, en son string silinir.

**Tech Stack:** C++20, LLVM 21, `build_clang.bat`, ctest (SERİ).

## Global Constraints

- **Davranış-koruyucu**: yeni pattern türü/sözdizimi/diagnostik YOK. Bugün derlenen her match aynen derlenir ve aynı kodu üretir; bugün hata olan aynı şekilde hata kalır.
- Her task sonunda: `build_clang.bat` temiz + `ctest --test-dir build-clang --output-on-failure` (ASLA `-j`; FOREGROUND 600000ms) 2340/2340.
- Geçiş süresince (Task 1'den string silinene dek) parser'da runtime doğrulama: `assert benzeri kontrol yerine` — `-fno-exceptions` ve assert'siz kod tabanı gereği kontrol şu biçimde yapılır: eşitsizlikte `diag_` YOKTUR parser'da hata üretmeden; bunun yerine Task 1'de eşitlik BİR KEZ test-suite koşusuyla kanıtlanır: geçici olarak eşitsizlikte `fprintf(stderr, "PATTERN-MISMATCH: %s vs %s\n", ...)` yazan kod eklenir, tam suite koşulur, stderr'de sıfır PATTERN-MISMATCH doğrulanır, fprintf Task 1 commit'inden ÖNCE ÇIKARILMAZ — Task 6'da stringle birlikte silinir. (Test çıktısı kirliliği: satır yalnız mismatch'te basılır; sıfır mismatch = sıfır kirlilik.)
- Commit formatı: `feat(pattern-ast): <açıklama>` (altyapı taskları `refactor(pattern-ast):`) + zorunlu Co-Authored-By/Claude-Session fragmanları.
- Sapma protokolü: gramer-dışı pattern keşfi (testlerde/örneklerde parser'ın yeni gramerine oturmayan) → DURDUR, controller'a raporla. Test kırmızı → task geri al.

## Pattern.h (normatif)

```cpp
#pragma once
#include "liva/Basic/SourceLocation.h"   // gerçek header adını Task 1'de mevcut AST include'larından doğrula
#include <memory>
#include <string>
#include <vector>

namespace liva {

class Pattern {
public:
    enum class Kind { Wildcard, Identifier, EnumCase, IntLiteral };
    explicit Pattern(Kind k, SourceRange r) : kind_(k), range_(r) {}
    virtual ~Pattern() = default;
    Kind getKind() const { return kind_; }
    SourceRange getRange() const { return range_; }
    // Parser'ın eski boşluksuz token-birleştirmesiyle BAYT-ÖZDEŞ metin.
    std::string toString() const;
private:
    Kind kind_;
    SourceRange range_;
};

class WildcardPattern : public Pattern {
public:
    explicit WildcardPattern(SourceRange r) : Pattern(Kind::Wildcard, r) {}
    static bool classof(const Pattern *p) { return p->getKind() == Kind::Wildcard; }
};

class IdentifierPattern : public Pattern {
public:
    IdentifierPattern(std::string name, SourceRange r)
        : Pattern(Kind::Identifier, r), name_(std::move(name)) {}
    const std::string &getName() const { return name_; }
    static bool classof(const Pattern *p) { return p->getKind() == Kind::Identifier; }
private:
    std::string name_;
};

class EnumCasePattern : public Pattern {
public:
    EnumCasePattern(std::string enumName, std::string caseName,
                    std::vector<std::unique_ptr<Pattern>> subs, SourceRange r)
        : Pattern(Kind::EnumCase, r), enumName_(std::move(enumName)),
          caseName_(std::move(caseName)), subpatterns_(std::move(subs)) {}
    const std::string &getEnumName() const { return enumName_; }   // bare Case ise boş
    const std::string &getCaseName() const { return caseName_; }
    bool hasParens() const { return hasParens_; }                   // Case() vs Case ayrımı toString için
    void setHasParens(bool v) { hasParens_ = v; }
    const std::vector<std::unique_ptr<Pattern>> &getSubpatterns() const { return subpatterns_; }
    static bool classof(const Pattern *p) { return p->getKind() == Kind::EnumCase; }
private:
    std::string enumName_, caseName_;
    bool hasParens_ = false;
    std::vector<std::unique_ptr<Pattern>> subpatterns_;
};

class IntLiteralPattern : public Pattern {
public:
    IntLiteralPattern(int64_t value, std::string text, SourceRange r)
        : Pattern(Kind::IntLiteral, r), value_(value), text_(std::move(text)) {}
    int64_t getValue() const { return value_; }
    const std::string &getText() const { return text_; }  // orijinal yazım (örn. negatif, hex)
    static bool classof(const Pattern *p) { return p->getKind() == Kind::IntLiteral; }
private:
    int64_t value_;
    std::string text_;
};

} // namespace liva
```

`toString()` (Pattern.cpp veya inline): Wildcard→`"_"`; Identifier→name; IntLiteral→text; EnumCase→`enumName + "." + caseName` (enumName boşsa yalnız caseName) + (hasParens ? `"(" + alt,virgülle + ")"` : `""`). DİKKAT: eski birleştirme BOŞLUKSUZ (`pattern += current_.getText()`), yani `Enum.Case(a,b)` — virgülden sonra boşluk YOK.

## Task listesi

---

### Task 0: Tüketici haritası (salt-okunur, commit yok)

**Files:** rapor → `.superpowers/sdd/task-0-report.md`

- [ ] **Step 1:** 7 tüketicide `arm.pattern` / `.pattern` / `getArms()` kullanım noktalarını file:line ile listele: `src/Parser/ParseExpr.cpp`, `src/Sema/TypeChecker.cpp` (visitMatchExpr:2803+ ve exhaustiveness:2703+), `src/IR/IRGenCall.cpp` (visitMatchExpr, parseMatchPattern:1629+, emitNestedPatternMatch:1719+, PatternInfo kullanan diğer yerler:1445+), `src/IR/IRGenExpr.cpp` (hangi bağlamda?), `src/DAP/DAPInterpreter.cpp` (~1010-1030), `src/AST/ASTPrinter.cpp`, `src/Macro/MacroExpander.cpp`. Her nokta için: string'den NE türetiliyor (wildcard testi mi, case adı mı, binding mi) — dönüşüm notuyla.
- [ ] **Step 2:** `MatchArm::bindings` alanını kim dolduruyor/okuyor — file:line.
- [ ] **Step 3:** Gramer kapsam taraması: `tests/` ve `examples/` içindeki TÜM match pattern'lerini çıkar (regex ile match bloklarını tara), spec'teki gramerin dışına düşen var mı raporla. Gramer: `_` | int-literal | Ident | Ident `(` alt `)` | Ident `.` Ident [`(` alt `)`], alt = virgülle ayrılmış pattern listesi (recursive).
- [ ] **Step 4:** `parseMatchPattern`'ın string'i NASIL yorumladığının tam haritası (negatif sayı? `-` token birleşimi? nested parenler?) — parser'ın yeni gramerinin birebir aynı kümeyi kabul ettiğini kanıtlamak için.

---

### Task 1: Pattern.h + parser çift-alan üretimi

**Files:** Create: `include/liva/AST/Pattern.h`, `src/AST/Pattern.cpp` (toString); Modify: `include/liva/AST/Expr.h` (MatchArm'a `std::unique_ptr<Pattern> patternNode;` + include), `src/Parser/ParseExpr.cpp` (parseMatchExpr: yeni `parsePattern()` recursive fonksiyonu; hem string hem Pattern üretimi + geçici PATTERN-MISMATCH stderr kontrolü), `src/AST/CMakeLists` girdisi gerekiyorsa (AST kaynak listesine Pattern.cpp — CMakeLists.txt `liva_ast` hedefini bul).

**Interfaces:**
- Produces: Pattern hiyerarşisi (normatif kod yukarıda), `Parser::parsePattern()` (private), `MatchArm::patternNode`.

- [ ] **Step 1:** Pattern.h'yi normatif koddan oluştur (SourceRange include'unu mevcut AST header'larından doğrula); Pattern.cpp'de toString.
- [ ] **Step 2:** MatchArm'a alan ekle (string ve bindings alanları KALIR).
- [ ] **Step 3:** ParseExpr.cpp'de `parsePattern()` yaz (gramer: spec + Task 0 Step 4 haritası); `parseMatchExpr` içinde: token-birleştirme AYNEN kalır (string alanına), ayrıca token akışının pattern kısmı `parsePattern()`'la ikinci kez... — DİKKAT: token akışı tek yönlü; iki kez parse edilemez. Çözüm: parseMatchExpr pattern bölümünü ÖNCE token listesine biriktirmek yerine, `parsePattern()` token'ları tüketirken aynı anda string'i de biriktirir (her advance'te `current_.getText()` ekleme — mevcut birleştirmeyle aynı sıra/aynı token seti). Yani tek geçiş, iki çıktı. **Task 0 zorunlulukları:** (a) negatif int pattern (`-1`) desteklenmeli — `-` + digit token çifti IntLiteralPattern'e birleşir (text="-1"); (b) bare `Ident(alt)` formu bugün yanlış-parse ediliyor ve repo'da hiç kullanılmıyor — yeni gramer spec gereği EnumCase üretir (bilinçli, raporlanmış sapma).
- [ ] **Step 4:** Geçici kontrol: pattern parse bitince `patternNode->toString() != pattern` ise `fprintf(stderr, "PATTERN-MISMATCH: ...")`.
- [ ] **Step 5:** Derle; tam seri suite; **stderr'de PATTERN-MISMATCH ara** (ctest çıktısını dosyaya yönlendirip grep) → sıfır olmalı. Testler 2340/2340.
- [ ] **Step 6:** Commit — `refactor(pattern-ast): Pattern AST hiyerarşisi + parser çift-alan üretimi`

---

### Task 2: IRGen dönüşümü

**Files:** Modify: `src/IR/IRGenCall.cpp` (visitMatchExpr + emitNestedPatternMatch → Pattern tüketir; `parseMatchPattern` ve string-reparse SİLİNİR; `PatternInfo` ya silinir ya Pattern'den doldurulan ince adaptöre iner — implementer kararı, raporlanır), `include/liva/IR/IRGen.h` (imzalar).

- [ ] **Step 1:** Task 0 haritasındaki IRGen noktalarını `arm.patternNode` üzerinden yeniden yaz; case-vs-binding çözümü (`subjectEnumType` kuralı) AYNEN korunur, sadece string yerine `IdentifierPattern`/`EnumCasePattern` üzerinden.
- [ ] **Step 2:** Derle + tam seri suite 2340/2340 (match codegen testleri kritik).
- [ ] **Step 3:** Commit — `refactor(pattern-ast): IRGen match codegen Pattern AST tüketiyor; string-reparse silindi`

---

### Task 3: TypeChecker dönüşümü

**Files:** Modify: `src/Sema/TypeChecker.cpp` (visitMatchExpr:2803+ binding tipleme; exhaustiveness:2703+ wildcard/Ok-Err kontrolleri).

- [ ] **Step 1:** Task 0 haritasındaki TypeChecker noktalarını `patternNode` üzerinden yeniden yaz; diagnostik davranışı birebir korunur.
- [ ] **Step 2:** Derle + tam seri suite (SemaTest'ler kritik) → **Step 3:** Commit — `refactor(pattern-ast): TypeChecker match kontrolü Pattern AST tüketiyor`

---

### Task 4: DAPInterpreter dönüşümü

**Files:** Modify: `src/DAP/DAPInterpreter.cpp` (~1010-1030 + Task 0'ın bulduğu diğer noktalar).

- [ ] **Step 1:** String karşılaştırmaları Pattern türlerine çevrilir (davranış birebir; DAP'ın desteklemediği pattern'ler bugün nasıl davranıyorsa öyle kalır).
- [ ] **Step 2:** Derle + tam seri suite (DAPTest) → **Step 3:** Commit — `refactor(pattern-ast): DAP interpreter Pattern AST tüketiyor`

---

### Task 5: ASTPrinter + string alanının silinmesi (Task 0 bulgularıyla güncellendi)

**Files:** Modify: `src/AST/ASTPrinter.cpp` (pattern yazdırma → `patternNode->toString()`), `tests/unit/ParserTest.cpp:1273` (`.pattern` assertion'ı → `patternNode` üzerinden eşdeğer assertion), `include/liva/AST/Expr.h` (`MatchArm::pattern` string alanı SİLİNİR; **`bindings` alanı da SİLİNİR** — Task 0 kanıtı: hiç yazılmıyor, tek okuyucu DAPInterpreter:1011-1020'deki ölü koşul; DAP'taki ölü okuma da temizlenir), `src/Parser/ParseExpr.cpp` (string biriktirme + PATTERN-MISMATCH kontrolü SİLİNİR). NOT: MacroExpander kapsam DIŞI — `.pattern` referansları alakasız `MacroArm::pattern` (Task 0 bulgusu); macro gövdeleri normal parseMatchExpr'den geçtiği için otomatik kapsanır.

- [ ] **Step 1:** ASTPrinter dönüşümü + ParserTest assertion güncellemesi.
- [ ] **Step 2:** `pattern` string alanını sil; derleme hataları kalan tüm string tüketicilerini gösterir — hepsini dönüştür (yeni keşifler raporlanır).
- [ ] **Step 3:** Kanıt: `grep -rn "arm\.pattern\b\|\.pattern\b" src/ --include=*.cpp` → MatchArm'ın string alanına referans SIFIR (başka yapıların .pattern üyeleri hariç — raporda ayıklanır).
- [ ] **Step 4:** Derle + tam seri suite 2340/2340 → **Step 5:** Commit — `refactor(pattern-ast): MatchArm string pattern alanı silindi — Pattern AST tek kaynak`

---

### Task 6: Kapanış

**Files:** Modify: `roadmap.md`

- [ ] **Step 1:** Tam seri suite 2340/2340; sayı değişmemiş.
- [ ] **Step 2:** roadmap.md 3. satır: `` Yapısal Pattern AST + eksik pattern türleri — **Faz A (AST altyapısı) tamamlandı (2026-07)**, Faz B (yeni pattern türleri) sırada `` .
- [ ] **Step 3:** Commit — `refactor(pattern-ast): Faz A tamam — MatchArm yapısal Pattern AST kullanıyor`
