# Yapısal Pattern Matching — Faz B: Yeni Pattern Türleri (Tasarım)

Tarih: 2026-07-23
Kapsam: Faz A'nın (Pattern AST, merge edildi) üzerine 5 yeni pattern türü + destekleyici sertleştirme. Kullanıcı onayı: A+B birlikte onaylandı (2026-07-23).
Tür: **Yeni dil özellikleri** — TDD zorunlu: her özellik önce başarısız testle gelir. Mevcut 2340 test yeşil kalır; toplam test sayısı artar.

## Faz A'dan taşınan sertleştirme ön-koşulları (final incelemeden)

1. `Pattern::getSpelling()` eklenir (bugün `toString()` ile aynı); Sema/IRGen'de **binding adı türeten** çağrılar getSpelling'e geçer — toString artık yalnızca görüntüleme içindir.
2. `TypeChecker::declarePatternSubBinding` açık-uçlu else'i kind-switch'e çevrilir (yeni tür eklendiğinde derleme uyarısıyla yakalanır).
3. Bilinen köşeler Faz B'de düzeltilir (artık davranış-koruma kısıtı yok): `-x` fallback'inde düşen `-`; `1 2` gibi artık-token dizileri zaten parse hatası veriyor (istenen davranış, spec'e bağlanır).
4. Test boşlukları kapatılır: negatif-int arm (`-1 =>`) çalışır hale getirilir ve test edilir (bugün `tag >= 0` filtresi negatif literal'leri sessizce eşleşmez yapıyor — düzeltilir), nested tag=-1 inert yolu diagnostiğe bağlanmaz (Faz C/exhaustiveness işi, kapsam dışı).

## Yeni pattern türleri (sözdizimi + semantik)

| Tür | Sözdizimi | Semantik |
|---|---|---|
| **Bool literal** | `true =>` / `false =>` | i1 karşılaştırma |
| **String literal** | `"GET" =>` | `liva_str_equal` ile karşılaştırma |
| **Float literal** | `3.14 =>` | `fcmp oeq` (bilinçli: tam eşitlik; dokümante edilir) |
| **Range** | `1..10 =>` (exclusive), `1..=10 =>` (inclusive) | int subject için `lo <= x && x < hi` / `<= hi`; uçlar int literal (negatif dahil) |
| **Or** | `1 \| 2 \| 3 =>`, `Color.Red \| Color.Blue =>` | alternatiflerden biri eşleşirse; **kısıt: alternatifler binding içeremez** (`err_pattern_or_binding` diagnostiği); exhaustiveness sayımında her alternatif ayrı case sayılır |
| **@ binding** | `n @ 1..=9 =>`, `s @ "x" =>` | pattern eşleşirse subject/slot değeri `n`'e bağlanır |
| **Tuple** | `(a, b) =>`, `(1, x) =>` | tuple subject destructure; arity uyuşmazlığı derleme hatası (`err_pattern_tuple_arity`); alt-pattern'ler recursive |

Yeni AST düğümleri (`Pattern.h`): `BoolLiteralPattern`, `StringLiteralPattern`, `FloatLiteralPattern`, `RangePattern{lo,hi,inclusive}`, `OrPattern{alternatives}`, `BindingPattern{name,sub}`, `TuplePattern{elements}`.

## Katman etkileri

- **Parser**: `parsePattern` genişler — literal token'lar, `(` ile tuple, sonek `|` (sol-assoc or listesi), `..`/`..=` (int literal sonrası), `ident @` öneki. Belirsizlik notu: `(` pattern başında tuple'dır (mevcut gramerde `(` yalnız `Case(` sonrasında geçerliydi); `Ident @` yeni; `Ident |` or-alternatifi.
- **Sema**: yeni kind'ler için tip kontrolü (subject tipi ile literal/range uyumu — `err_pattern_type_mismatch`), or-binding kısıtı, tuple arity, `@` binding tip ataması (subject/slot tipi), exhaustiveness: bool `true|false` çifti exhaustive sayılır; or alternatifleri coveredCases'e ayrı ayrı işlenir; diğer türler wildcard gerektirmeye devam eder.
- **IRGen**: her kind için karşılaştırma/dallanma codegen'i; `@` için slot değerini alloca'ya bağlama; tuple için eleman GEP + recursive eşleşme; or için alternatif-OR blok zinciri; PatternInfo bu türler için ya genişletilir ya da (tercih) visitMatchExpr'de Pattern doğrudan tüketilir — implementasyon kararı planda.
- **DAP interpreter**: yeni türler desteklenmez → arm eşleşmez (mevcut sınırlı davranışla tutarlı); rapor edilir, ayrı iş.
- **Yeni diagnostikler**: `err_pattern_or_binding`, `err_pattern_tuple_arity`, `err_pattern_type_mismatch` (DiagnosticKinds.def).
- **Docs**: LANGUAGE-REFERENCE (EN+TR) pattern bölümü + COOKBOOK örneği. Tree-sitter/VSCode TextMate/LSP güncellemeleri kapsam dışı (ayrı iş, roadmap notu).

## Test stratejisi (TDD)

Her özellik task'ı: (1) ParserTest — AST şekli; (2) SemaTest — kabul + yeni diagnostikler; (3) RuntimeExecTest — derle-çalıştır davranış (eşleşen/eşleşmeyen kollar, binding değerleri, guard etkileşimi). Mevcut 2340 test yeşil kalır.

## Kapsam dışı

Struct destructuring pattern; if-let/while-let'te tam pattern; exhaustiveness'in enum-dışı tam analizi; DAP yeni-tür desteği; editör gramerleri; case-vs-binding kuralının Sema+IRGen ortak helper'da birleştirilmesi (ayrı refactor).
