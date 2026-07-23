# Runtime ABI `.def` Tablosu — Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `IRGen::createRuntimeDecls` (462 ad) ve `JITEngine` REG listesi (244 ad) tek `src/IR/RuntimeFunctions.def` X-macro tablosundan beslenir.

**Architecture:** Spec: `docs/superpowers/specs/2026-07-23-runtime-abi-def-design.md`. İki makro adı (`LIVA_RT` / `LIVA_RT_JIT` + `_VA` varyantları), 6 tip kodu (`VOID, PTR, I8, I32, I64, F64`). `.def` mevcut koddan script'le üretilir; eşdeğerlik IR-declare-diff ve REG-liste-diff ile kanıtlanır.

**Tech Stack:** C++20, LLVM 21, CMake+Ninja (`build_clang.bat`), ctest (SERİ), Python 3 (çıkarım script'i, scratchpad'de).

## Global Constraints

- Davranış-koruyucu: decl imzaları ve REG kümesi birebir korunur. Bug/tutarsızlık görülürse düzeltilmez, raporlanır.
- Her task sonunda: `build_clang.bat` temiz + `ctest --test-dir build-clang --output-on-failure` (ASLA `-j`; FOREGROUND, 600000ms) 2340/2340.
- Scratchpad: `C:\Users\Kadir\AppData\Local\Temp\claude\F--Cpp-Projects-liva-lang\6cd7e925-e4dd-4fea-ad87-b79eaee75c70\scratchpad` — script'ler ve baseline dosyaları buraya, repo'ya COMMIT EDİLMEZ.
- Commit formatı: `refactor(runtime-abi): <açıklama>` + zorunlu Co-Authored-By/Claude-Session fragmanları.

## `.def` biçimi (normatif)

```cpp
// RuntimeFunctions.def — runtime ABI'nin tek kaynağı.
// LIVA_RT(name, RET, PARAMS)        IRGen decl (native link çözer)
// LIVA_RT_JIT(name, RET, PARAMS)    IRGen decl + JIT REG
// LIVA_RT_VA / LIVA_RT_JIT_VA       varargs varyantları
// PARAMS: RT_ARGS(kod, ...) — parametresiz için RT_ARGS()
// Tüketici, kullanmadığı makroları boş tanımlamak ZORUNDADIR.
LIVA_RT_JIT(liva_str_dup, PTR, RT_ARGS(PTR))
```

## Tüketici açılımları (normatif)

**IRGen (`createRuntimeDecls` yeni gövdesi):**
```cpp
void IRGen::createRuntimeDecls() {
    auto *ptrTy = llvm::PointerType::getUnqual(*context_);
    auto ty = [&](RtTypeCode c) -> llvm::Type * {
        switch (c) {
        case RT_VOID: return builder_->getVoidTy();
        case RT_PTR:  return ptrTy;
        case RT_I8:   return builder_->getInt8Ty();
        case RT_I32:  return builder_->getInt32Ty();
        case RT_I64:  return builder_->getInt64Ty();
        case RT_F64:  return builder_->getDoubleTy();
        }
        return ptrTy;
    };
    auto decl = [&](const char *name, RtTypeCode ret,
                    std::initializer_list<RtTypeCode> params, bool varargs) {
        std::vector<llvm::Type *> ps;
        for (auto p : params) ps.push_back(ty(p));
        module_->getOrInsertFunction(
            name, llvm::FunctionType::get(ty(ret), ps, varargs));
    };
#define RT_ARGS(...) { __VA_ARGS__ }
#define LIVA_RT(name, ret, params)        decl(#name, ret, params, false);
#define LIVA_RT_JIT(name, ret, params)    decl(#name, ret, params, false);
#define LIVA_RT_VA(name, ret, params)     decl(#name, ret, params, true);
#define LIVA_RT_JIT_VA(name, ret, params) decl(#name, ret, params, true);
#include "RuntimeFunctions.def"
#undef LIVA_RT
#undef LIVA_RT_JIT
#undef LIVA_RT_VA
#undef LIVA_RT_JIT_VA
#undef RT_ARGS
}
```
`RtTypeCode` enum'u (`RT_VOID..RT_F64`) IRGen.h'ye ya da .def'in başına yakın bir header'a eklenir (karar: `include/liva/IR/RuntimeTypeCodes.h`, her iki tüketici include eder — JIT tarafı tip kodlarını kullanmasa da parametre listesi derlenebilir olmalı; JIT açılımında `ret`/`params` yok sayılır, bu yüzden aslında JIT'te enum gerekmez ama header zararsızdır).

**JITEngine (REG bloğu yerine):**
```cpp
#define RT_ARGS(...)
#define LIVA_RT(name, ret, params)
#define LIVA_RT_VA(name, ret, params)
#define LIVA_RT_JIT(name, ret, params)    REG(name);
#define LIVA_RT_JIT_VA(name, ret, params) REG(name);
#include "liva/../../src/IR/RuntimeFunctions.def"  // gerçek yol Task 2'de include-path'e göre belirlenir
#undef ...
```
(`.def` konumu: `src/IR/RuntimeFunctions.def`; JIT include yolu CMake include dizinlerine göre Task 2'de netleştirilir — gerekiyorsa `.def` `include/liva/IR/` altına konur; karar implementer'ındır, raporlanır.)

---

### Task 0: Baseline + çıkarım script'i (commit yok)

**Files:** scratchpad'de: `extract_rt_def.py`, `baseline.ll`, `baseline-declares.txt`, `baseline-reg.txt`, `RuntimeFunctions.def.draft`

- [ ] **Step 1: IR baseline** — `build-clang/livac.exe --emit-ir examples/hello.liva -o <scratchpad>/baseline.ll` (bayrak sözdizimini `--help` ile doğrula). `grep '^declare' baseline.ll | sort > baseline-declares.txt`; satır sayısını raporla (≈462+ beklenir; createRuntimeDecls koşulsuz çalıştığından tüm decl'ler tek dump'ta).
- [ ] **Step 2: REG baseline** — `grep -oE 'REG\(liva_[a-z_0-9]+\)|REG\([a-z_0-9]+\)' src/JIT/JITEngine.cpp | sort > baseline-reg.txt`; 244 satır beklenir.
- [ ] **Step 3: Çıkarım script'i** — `extract_rt_def.py`: `createRuntimeDecls` gövdesini parse eder: (a) tip-değişkeni atamalarını (`auto *X = llvm::FunctionType::get(RET, {PARAMS}, VA);`) çözer, (b) her `getOrInsertFunction("name", tyVar)` çağrısını o imzayla eşler, (c) inline `FunctionType::get` kullanan çağrıları da yakalar, (d) llvm tip ifadelerini 6 koda eşler (`i8PtrTy/ptrTy→PTR` vb.), (e) adı `baseline-reg.txt`'te olanlara `LIVA_RT_JIT`, olmayanlara `LIVA_RT` (varargs'a `_VA`) yazar, (f) `RuntimeFunctions.def.draft` üretir.
- [ ] **Step 4: Kapsam kanıtı** — draft'taki ad kümesi == createRuntimeDecls'teki `getOrInsertFunction` ad kümesi (script raporlar; eksik/fazla varsa DURDUR, raporla). Ayrıca 6 koda eşlenemeyen imza kalıp kalmadığını raporla (kalırsa o girdiler draft'a `// HAND` yorumuyla işaretlenir ve Task 1'de elle-yazım bölümünde tutulur).
- [ ] **Step 5: REG⊆decl kontrolü** — `baseline-reg.txt`'teki her ad draft'ta var mı? Eksikler (REG'li ama decl'siz) raporlanır ve draft'a `LIVA_RT_JIT` olarak EKLENMEZ — bunlar JIT bloğunda elle bırakılır (davranış koruma; sapma raporu).

---

### Task 1: `.def` + createRuntimeDecls dönüşümü

**Files:**
- Create: `src/IR/RuntimeFunctions.def` (draft'tan), `include/liva/IR/RuntimeTypeCodes.h`
- Modify: `src/IR/IRGen.cpp` (createRuntimeDecls gövdesi), gerekirse `include/liva/IR/IRGen.h`

**Interfaces:**
- Produces: `RuntimeFunctions.def` (normatif biçimde), `RtTypeCode` enum'u.

- [ ] **Step 1:** `RuntimeTypeCodes.h` oluştur: `enum RtTypeCode { RT_VOID, RT_PTR, RT_I8, RT_I32, RT_I64, RT_F64 };` (namespace liva, include guard).
- [ ] **Step 2:** Draft'ı `src/IR/RuntimeFunctions.def` olarak yerleştir (dosya başına açıklama bloğu normatif biçimden).
- [ ] **Step 3:** `createRuntimeDecls` gövdesini normatif IRGen açılımıyla değiştir; `// HAND` işaretli girdiler (varsa) tablo açılımından SONRA elle-yazım olarak korunur.
- [ ] **Step 4:** Derle (`build_clang.bat`).
- [ ] **Step 5: IR eşdeğerlik kanıtı** — aynı komutla yeni dump; `grep '^declare' | sort` çıktısı `baseline-declares.txt` ile `diff` → **sıfır fark**. Fark varsa DURDUR, farkları raporla, düzelt, tekrar.
- [ ] **Step 6:** Seri ctest 2340/2340.
- [ ] **Step 7:** Commit — `refactor(runtime-abi): createRuntimeDecls RuntimeFunctions.def tablosundan üretiliyor`

---

### Task 2: JIT REG dönüşümü

**Files:** Modify: `src/JIT/JITEngine.cpp`; gerekirse `.def` taşıma/CMake include ayarı.

- [ ] **Step 1:** REG bloğunu (244 satır) normatif JIT açılımıyla değiştir; Task 0 Step 5'te tespit edilen decl'siz-REG'li adlar (varsa) elle REG olarak blok altında kalır. `.def` include yolunu çöz (tercih: `.def`'i `src/IR/`de tut, CMake'te JIT hedefine include dizini ekle ya da göreli include; seçimi raporla).
- [ ] **Step 2: REG kümesi kanıtı** — derleme sonrası JITEngine.o'daki kayıt kümesini doğrulamak yerine kaynak-düzeyi kanıt: `.def`'teki `LIVA_RT_JIT*` adları + elle kalan REG'ler == `baseline-reg.txt` (sort+diff, sıfır fark).
- [ ] **Step 3:** Derle → **Step 4:** Seri ctest 2340/2340 (REPL/JIT testleri dahil) → **Step 5:** Commit — `refactor(runtime-abi): JIT REG listesi RuntimeFunctions.def'ten üretiliyor`

---

### Task 3: Kapanış

**Files:** Modify: `roadmap.md`

- [ ] **Step 1:** `wc -l src/IR/IRGen.cpp src/IR/RuntimeFunctions.def src/JIT/JITEngine.cpp` — IRGen.cpp'nin ~700+ satır küçüldüğünü raporla.
- [ ] **Step 2:** Tam seri test 2340/2340; sayı değişmemiş.
- [ ] **Step 3:** roadmap.md öncelik tablosu 2. satırı: `` Runtime ABI'yi tek `.def` tablosuna indir — **tamamlandı (2026-07, RuntimeFunctions.def)** ``.
- [ ] **Step 4:** Commit — `refactor(runtime-abi): runtime ABI tek .def tablosunda — roadmap #2 tamam`

## Sapma protokolü

- 6 koda eşlenemeyen imza → `// HAND` bölümünde elle kalır, raporlanır.
- REG'li-ama-decl'siz ad → JIT bloğunda elle kalır, raporlanır.
- IR-declare diff'i sıfırlanamıyorsa → task geri alınır, fark analizi raporlanır; SONRAKI task'a geçilmez.
- Test kırmızı → task geri al, raporla.
