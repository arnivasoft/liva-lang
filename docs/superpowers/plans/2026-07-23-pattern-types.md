# Yeni Pattern Türleri (Faz B) — Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match ifadesine 5 yeni pattern türü: bool/string/float literal, range, or, `@` binding, tuple — TDD ile, spec: `docs/superpowers/specs/2026-07-23-pattern-types-design.md`.

**Architecture:** Her özellik dikey dilim: Pattern düğümü + parser + Sema + IRGen + üç katman test (Parser/Sema/RuntimeExec). Task 1 sertleştirme ön-koşullarını (getSpelling, kind-switch, negatif-int fix) atar; sonraki task'lar özellik ekler.

**Tech Stack:** C++20, LLVM 21, `build_clang.bat`, ctest (SERİ), GoogleTest.

## Global Constraints

- **TDD zorunlu**: her özellik adımında önce başarısız test(ler) yazılır, koşulup KIRMIZI doğrulanır, sonra implementasyon, sonra YEŞİL doğrulanır. Rapora RED/GREEN kanıtı (komut + ilgili çıktı) yazılır.
- Her task sonunda: `build_clang.bat` temiz + TAM seri suite (`ctest --test-dir build-clang --output-on-failure`, ASLA `-j`, FOREGROUND 600000ms): önceki tüm testler + bu task'ın yenileri yeşil. Toplam test sayısı raporlanır (artar).
- Test dosyaları mevcutları izler: `tests/unit/ParserTest.cpp`, `tests/unit/SemaTest.cpp`, `tests/unit/RuntimeExecTest.cpp` — mevcut test adlandırma/yapı kalıplarını kopyala (örn. RuntimeExec testleri kaynak string derleyip çalıştırır, çıktıyı doğrular; dosyadaki mevcut match testlerine bak).
- Yeni diagnostikler `include/liva/Common/DiagnosticKinds.def`'e DIAG kalıbıyla eklenir (mevcut err_ girdilerini örnek al).
- Kind-switch'ler default'suz yazılır (`-Wswitch` yeni tür eklemeyi yakalasın).
- DAP: yeni pattern türleri eşleşmez-davranır; her task DAP'a DOKUNMAZ (Task 1'deki kind-switch hariç).
- Commit formatı: `feat(pattern): <özellik>` (Task 1: `refactor(pattern):`) + zorunlu trailer'lar:
Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
- Sapma protokolü: parser belirsizliği/Sema-IRGen uyuşmazlığı çözülemiyorsa BLOCKED raporla; test kırmızı bırakılamaz.

## Yeni Pattern düğümleri (normatif — Task'larda eklenir)

```cpp
class BoolLiteralPattern : public Pattern { bool value_; };            // Kind::BoolLiteral
class StringLiteralPattern : public Pattern { std::string value_; };   // Kind::StringLiteral (value = unescaped içerik; spelling orijinal kaynak)
class FloatLiteralPattern : public Pattern { double value_; std::string text_; }; // Kind::FloatLiteral
class RangePattern : public Pattern {                                  // Kind::Range
    std::unique_ptr<Pattern> lo_, hi_;  // IntLiteralPattern (negatif olabilir)
    bool inclusive_; };
class OrPattern : public Pattern {                                     // Kind::Or
    std::vector<std::unique_ptr<Pattern>> alternatives_; };            // ≥2 eleman
class BindingPattern : public Pattern {                                // Kind::Binding
    std::string name_; std::unique_ptr<Pattern> sub_; };
class TuplePattern : public Pattern {                                  // Kind::Tuple
    std::vector<std::unique_ptr<Pattern>> elements_; };
```
Her yeni tür: `Kind` enum'una girdi, `classof`, `toString()`/`getSpelling()` case'i (Or: ` | ` ile mi boşluksuz `|` ile mi — kaynaktaki yazımdan bağımsız normalize `|` boşluksuz; Range: `lo..hi`/`lo..=hi`; Binding: `name@sub`; Tuple: `(a,b)`).

## Parser genişletmesi (normatif gramer)

```
pattern      := orPattern
orPattern    := primary ( '|' primary )*          // ≥1 '|' varsa OrPattern
primary      := binding
binding      := IDENT '@' primary                  // lookahead: IDENT ardından '@'
              | atom
atom         := '_' | boolLit | stringLit | floatLit
              | intLit [ rangeSuffix ]             // '..' | '..=' sonrası intLit → RangePattern
              | '(' pattern (',' pattern)+ ')'     // TuplePattern (tek elemanlıysa hata değil: mevcut gramerde '(' pattern başında yoktu; tek eleman = gruplama sayılmaz, err_pattern_tuple_arity yerine parser diagnostiği değil — KARAR: tuple en az 2 eleman, tek elemanlı '(p)' parse hatası)
              | IDENT [ '(' pattern-list ')' ]     // mevcut: bare case / Case(subs)
              | IDENT '.' IDENT [ '(' pattern-list ')' ]  // mevcut
```
Negatif int: mevcut `-`+digit birleşimi korunur; range uçlarında da geçerli (`-5..=5`). `-` sonrası rakam-değilse artık parser hatası (Faz A'daki sessiz `-` düşürme düzeltilir — davranış-koruma kısıtı kalktı).

## Task listesi

### Task 1: Sertleştirme + negatif-int fix (refactor + küçük davranış düzeltmesi)
**Files:** `include/liva/AST/Pattern.h`/`src/AST/Pattern.cpp` (getSpelling), `src/Sema/TypeChecker.cpp` (declarePatternSubBinding kind-switch + getSpelling), `src/IR/IRGenCall.cpp` (IntLiteral binding-adı getSpelling; negatif-int arm düzeltmesi: `tag >= 0` filtresi yüzünden `-1 =>` kolu hiç eşleşmiyor — int-literal kollar tag yerine ayrı karşılaştırma yoluna alınır ya da işaretli karşılaştırma düzeltilir, mevcut visitMatchExpr yapısını oku ve raporla), `src/Parser/ParseExpr.cpp` (`-` sonrası rakam-değilse diagnostik).
- [ ] TDD: önce RuntimeExecTest'e `-1 =>` kolu içeren KIRMIZI test; ParserTest'e `-` hata testi.
- [ ] İmplement; tam suite yeşil; commit `refactor(pattern): getSpelling + kind-switch sertleştirme, negatif-int arm düzeltmesi`.

### Task 2: Bool + String + Float literal pattern'leri
- [ ] TDD: ParserTest (AST şekli), SemaTest (tip uyuşmazlığı `err_pattern_type_mismatch` — DiagnosticKinds.def'e ekle), RuntimeExecTest (`true/false` kollu bool match; `"GET"/"POST"` string match; float match) önce KIRMIZI.
- [ ] Pattern düğümleri + parser + Sema tip kontrolü + IRGen (bool: icmp eq; string: liva_str_equal çağrısı; float: fcmp oeq).
- [ ] Tam suite; commit `feat(pattern): bool/string/float literal pattern'leri`.

### Task 3: Range pattern
- [ ] TDD: ParserTest (`1..10`, `1..=10`, `-5..=5` AST), SemaTest (int-olmayan subject'e range → err_pattern_type_mismatch), RuntimeExecTest (sınır değerleri: lo dahil, exclusive'de hi hariç, inclusive'de dahil; negatif uçlar) — KIRMIZI önce.
- [ ] RangePattern + parser (intLit sonrası `..`/`..=` lookahead) + Sema + IRGen (`icmp sge` + `icmp slt/sle` AND).
- [ ] Tam suite; commit `feat(pattern): range pattern (a..b, a..=b)`.

### Task 4: Or pattern
- [ ] TDD: ParserTest (`1|2|3`, `Color.Red|Color.Blue` AST), SemaTest (`err_pattern_or_binding`: alternatifte binding/`@` → hata; DiagnosticKinds.def'e ekle), RuntimeExecTest (int or, enum-case or, guard ile birlikte), exhaustiveness testi (tüm case'ler or'la kapsanınca unreachable-wildcard uyarısı davranışı mevcut kurala uygun) — KIRMIZI önce.
- [ ] OrPattern + parser (`|` sol-assoc) + Sema (binding yasağı, coveredCases'e alternatifleri ayrı işle) + IRGen (alternatif test bloklarının OR zinciri — biri eşleşirse gövde).
- [ ] Tam suite; commit `feat(pattern): or pattern (p1 | p2)`.

### Task 5: @ binding pattern
- [ ] TDD: ParserTest (`n @ 1..=9` AST), SemaTest (n'in tipi subject/slot tipi; iç pattern'de binding yasağı OR kuralıyla tutarlı), RuntimeExecTest (`n @ 1..=9 => n` değer doğrulama; `s @ "x"`; enum slotunda `v @ Inner.Val(k)` KAPSAM DIŞI ise raporla — minimum: üst-düzey subject @) — KIRMIZI önce.
- [ ] BindingPattern + parser (IDENT `@` lookahead) + Sema + IRGen (pattern eşleşme yolunda değeri alloca'ya bağla).
- [ ] Tam suite; commit `feat(pattern): @ binding pattern`.

### Task 6: Tuple pattern
- [ ] TDD: ParserTest (`(a, b)`, `(1, x)` AST; tek elemanlı `(p)` hata), SemaTest (arity uyuşmazlığı `err_pattern_tuple_arity` — DiagnosticKinds.def'e ekle; eleman tip ataması), RuntimeExecTest (tuple subject destructure, karışık literal+binding elemanlar, nested `( (a,b), c )` MÜMKÜNSE — değilse raporla) — KIRMIZI önce.
- [ ] TuplePattern + parser + Sema + IRGen (tuple eleman GEP + recursive eşleşme; TupleLiteralExpr/tuple tip temsili için mevcut visitTupleLiteralExpr'i incele).
- [ ] Tam suite; commit `feat(pattern): tuple destructuring pattern`.

### Task 7: Dokümantasyon + kapanış
- [ ] `docs/en/LANGUAGE-REFERENCE.md` + `docs/tr/LANGUAGE-REFERENCE.md` pattern-matching bölümüne 5 yeni tür (mevcut bölüm biçimini izle, örneklerle); `docs/en/COOKBOOK.md` + `docs/tr/COOKBOOK.md`'ye kısa tarif.
- [ ] `roadmap.md` 3. satır: `` **Faz A+B tamamlandı (2026-07)** — kalan: struct destructuring, if-let tam pattern, editör gramerleri (ayrı işler) ``.
- [ ] Tam suite son koşum; toplam test sayısı raporda; commit `docs(pattern): yeni pattern türleri dokümantasyonu + roadmap`.
