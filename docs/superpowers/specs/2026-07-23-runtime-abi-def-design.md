# Runtime ABI → Tek `.def` Tablosu — Tasarım (Roadmap #2)

Tarih: 2026-07-23
Kapsam: Runtime fonksiyon ABI'sinin üç elle-senkron kopyasından ikisinin tek kaynağa indirilmesi: (1) `IRGen::createRuntimeDecls` (`src/IR/IRGen.cpp:393+`, 462 benzersiz ad, 483 `getOrInsertFunction`), (2) `JITEngine.cpp` `REG(...)` listesi (244 sembol). C++ tanımları (`stdlib/runtime/*.cpp`) elle kalır; decltype-traits doğrulayıcısı **kapsam dışı** (ileri iş).
Tür: Davranış-koruyucu refactor. Her adımda 2340 test yeşil + LLVM IR `declare` eşdeğerliği kanıtı.

## Tespitler

- `createRuntimeDecls` yalnızca 6 LLVM tipi kullanıyor: `ptr, i8, i32, i64, double, void` (+ printf için varargs). Struct/array/vector YOK — doğrulandı.
- JIT, REG'lenmeyen sembolleri `DynamicLibrarySearchGenerator::GetForCurrentProcess` ile çözüyor; REG kümesi (244) bilinçli bir alt küme. REG makrosu `&name` aldığından yalnızca JITEngine TU'sunda bildirimi görünür semboller REG'lenebilir.
- `createRuntimeDecls` modül başına koşulsuz çağrıldığından HERHANGİ bir programın `--emit-ir` çıktısı 462 decl'in tamamını içerir → eşdeğerlik kanıtı için tek dump yeter.

## Tasarım

**`src/IR/RuntimeFunctions.def`** — X-macro, iki makro adı (JIT üyeliği makro adıyla, bayrakla değil — preprocessor bayrak dallanamaz ve REG `&name` gerektirir):

```cpp
// LIVA_RT(name, RET, PARAMS)      — yalnızca IRGen decl (native link çözer)
// LIVA_RT_JIT(name, RET, PARAMS)  — IRGen decl + JIT'te REG (sembol JITEngine'de görünür olmalı)
// PARAMS: RT_ARGS(...) sarmalayıcısıyla; varargs için LIVA_RT_VA / LIVA_RT_JIT_VA varyantı
LIVA_RT_JIT(liva_str_dup,    PTR, RT_ARGS(PTR))
LIVA_RT_JIT(liva_str_concat, PTR, RT_ARGS(PTR, PTR))
LIVA_RT_VA (printf,          I32, RT_ARGS(PTR))
LIVA_RT    (liva_ui_on_click, VOID, RT_ARGS(I32, PTR, PTR, I32))
```

Tip kodları: `VOID, PTR, I8, I32, I64, F64`.

**Tüketici 1 — `IRGen::createRuntimeDecls`:** kod→`llvm::Type*` eşleyici + `.def` include'u; mevcut ~800 satırlık elle yazım ~40 satıra iner. `getOrInsertFunction` idempotent olduğundan tablo sırası/mükerrerlik davranışı etkilemez.

**Tüketici 2 — `JITEngine`:** REG bloğu `.def` include'una dönüşür; yalnızca `LIVA_RT_JIT*` makroları REG'e açılır, `LIVA_RT*` boş tanımlanır. Mevcut 244 kümesi birebir korunur.

## Doğrulama stratejisi

1. **`.def` elle yazılMAZ**: mevcut `createRuntimeDecls` gövdesinden script'le üretilir (462 imzayı elle aktarmak = miscompile riski).
2. **IR eşdeğerlik kanıtı**: refactor öncesi `livac --emit-ir` baseline'ından `declare` satırları çıkarılır; dönüşüm sonrası aynı programın dump'ıyla diff — **sıfır fark** şartı (sıralamadan bağımsız, sort edilmiş karşılaştırma).
3. **REG kümesi kanıtı**: dönüşüm öncesi/sonrası REG'lenen sembol listesi (244 ad) birebir aynı olmalı.
4. Her adımda `build_clang.bat` + seri ctest 2340/2340 (JIT yolu REPL/RuntimeExec testlerinde koşuyor).

## Kazanç

Yeni runtime fonksiyonu eklemek = `.def`'e 1 satır + C++ tanımı. "createRuntimeDecls unutuldu → getOrPanic çöktü" hata sınıfı kapanır; IRGen imzası ile JIT kaydı tek kaynaktan.

## Kapsam dışı

- `runtime.h` C++ imzalarının decltype-traits `static_assert` doğrulayıcısı (faz 3, ileri iş).
- Çağrı-yeri `getOrInsertFunction` idiyomlarının (IRGenCall* dosyalarındaki) tabloya bağlanması.
- Davranış değişikliği, yeni fonksiyon ekleme/çıkarma.
