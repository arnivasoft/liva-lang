# CI -Werror + Fixture Hard-Fail — Implementasyon Planı (Roadmap #8)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `LIVA_WERROR` CMake opsiyonu + iki yerel derleyicide sıfır-uyarı + CI 3 job'da opsiyon açık + IntegrationTest dosya-bulunamadı skip'leri hard-fail. Spec: `docs/superpowers/specs/2026-07-24-werror-fixture-hardfail-design.md`.

**Architecture:** Opsiyon kök CMakeLists.txt'te, yalnız proje hedeflerine uygulanır (`_deps` hariç). Uyarı temizliği gerçek düzeltmeyle (bastırma son çare + gerekçeli). Fixture hard-fail mekanik dönüşüm (GTEST_SKIP → FAIL).

**Tech Stack:** CMake, Clang 21 (clang-cl, build-clang), MinGW GCC 15.2, GitHub Actions.

## Global Constraints

- Branch: `feat/werror-fixture-hardfail` (main'den).
- Her task sonunda: Clang tam derleme + TAM seri ctest (`ctest --test-dir build-clang --output-on-failure`, ASLA `-j`, FOREGROUND, 600000ms). Taban: **2489 yeşil** (3 opt-in skip). Davranış değişikliği beklenmiyor — sayı 2489 kalmalı.
- Commit trailer'ları zorunlu:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216`
- MinGW build notları (hafıza): test-only destek; `-fno-exceptions`; ayrı build dizini kullan (`build-mingw-werror` gibi — mevcut `build/` varsa dokunma).
- Env-kapılı canlı-test skip'leri (RuntimeExecTest PG/HTTP/WS) DEĞİŞMEZ.
- Uyarı düzeltmeleri DAVRANIŞ DEĞİŞTİRMEZ (unused → kaldır/`(void)`, sign-compare → açık cast, vb.); şüpheli durumda düzeltme yerine raporla.
- Sapma protokolü: bir uyarı gerçek bir hataya işaret ediyorsa (ör. gerçekten yanlış kod) düzeltme ayrı değerlendirilir — controller'a raporla, sessizce davranış değiştirme.

---

### Task 0: Keşif — CMake uyarı yapısı + uyarı envanteri (salt-okunur, commit yok)

Rapor → `.superpowers/sdd/task-0-report.md`:

- [ ] (a) Kök `CMakeLists.txt` + `tests/CMakeLists.txt`: uyarı bayrakları şu an nasıl veriliyor (global `add_compile_options` mı, hedef-bazlı mı; hangi bayraklar); `_deps` (GoogleTest) bu bayrakları miras alıyor mu; `LIVA_WERROR`'un `_deps`'i etkilemeden uygulanacağı doğru nokta (öneri + gerekçe, file:line).
- [ ] (b) Clang uyarı envanteri: `build-clang`'ın CMake cache'ine dokunmadan tam derlemedeki uyarıları topla — `cmake --build build-clang 2>&1 | grep -iE "warning"` (temiz artımlı build uyarı göstermez; gerekirse `ninja -t clean` yerine dokunulmuş TU listesi — pratik yol: tüm .cpp'lere dokunmadan `cmake --build build-clang --clean-first` AYRI bir dizinde DEĞİL — mevcut build-clang'ı clean-first yapmak 20-30 dk sürebilir; alternatif: yeni `build-clang-werror` dizini configure et (aynı toolchain bayraklarıyla — `build_clang.bat`'in verdiği ayarları kopyala) ve orada tam derle). Uyarıların dosya+satır+tür envanteri.
- [ ] (c) MinGW uyarı envanteri: `cmake -G "MinGW Makefiles" -B build-mingw-werror -DCMAKE_BUILD_TYPE=Release` + spec'teki 10 CI test hedefini derle; uyarı envanteri. (MinGW yolu/PATH kontrolü; gcc 15.2.)
- [ ] (d) `IntegrationTest.cpp`'deki 10 GTEST_SKIP sitesinin tam satır listesi + her birinin deseni (readFile-fail mi başka koşul mu); RuntimeExecTest'in 3 env-kapılı skip'inin satırları (DOKUNULMAYACAK liste).
- [ ] (e) `ci.yml` üç job'ın Configure satırları (değiştirilecek yerler, satır numaralarıyla).

### Task 1: LIVA_WERROR opsiyonu + uyarı temizliği + CI (TDD-vari: önce KIRMIZI derleme kanıtı)

**Files:** Modify: kök `CMakeLists.txt` (Task 0 (a) noktası), uyarı çıkan kaynak dosyalar (Task 0 (b)/(c) envanteri), `.github/workflows/ci.yml`.

- [ ] **Step 1: Opsiyonu ekle** (Task 0 (a) önerisine göre; şablon — keşif farklı derse rapora göre uyarla):

```cmake
option(LIVA_WERROR "Treat compiler warnings as errors (CI)" OFF)
# ... proje hedeflerinin bayrak aldığı noktada:
if(LIVA_WERROR)
  if(MSVC)
    add_compile_options(/WX)
  else()
    add_compile_options(-Werror)
  endif()
endif()
```

`_deps` etkilenmesin: opsiyon bloğu `FetchContent_MakeAvailable(googletest)` ÇAĞRISINDAN SONRA gelmeli ya da hedef-bazlı `target_compile_options` kullanılmalı — Task 0 (a) hangisini söylüyorsa o.
- [ ] **Step 2: KIRMIZI kanıt** — `-DLIVA_WERROR=ON` ile (Task 0 (b) dizininde) derleme: mevcut uyarılar hata olarak düşürmeli. Çıktı rapora.
- [ ] **Step 3: Uyarıları düzelt** — envanterdeki her uyarı: unused-variable → kaldır veya `(void)x;`; unused-function → kaldır ya da kullanan test ekle (davranış koruyarak; şüphedeyse `[[maybe_unused]]`); sign-compare → açık `static_cast`; deprecation (MSVC CRT) → mevcut proje yaklaşımı (`_CRT_SECURE_NO_WARNINGS` tanımı zaten varsa dokunma; yoksa hedefe define ekle ve raporla). Her düzeltme davranış-nötr.
- [ ] **Step 4: YEŞİL kanıt** — iki derleyicide de `-DLIVA_WERROR=ON` tam derleme SIFIR uyarı/hata; `LIVA_WERROR` KAPALI normal configure'un değişmediğini de doğrula (`cmake --build build-clang` dokunulmamış — cache'i bozmadığını kanıtla).
- [ ] **Step 5: ci.yml** — 3 job'ın Configure satırına `-DLIVA_WERROR=ON` (Task 0 (e) satırları). YAML'ı yerel lint et (`python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))"`).
- [ ] **Step 6: Tam seri ctest (build-clang, opsiyon OFF normal build) 2489 yeşil + commit** — `build(ci): LIVA_WERROR opsiyonu + sıfır-uyarı temizliği + CI'da -Werror` + trailer'lar.

### Task 2: Fixture-skip hard-fail + roadmap kapanışı

**Files:** Modify: `tests/unit/IntegrationTest.cpp` (Task 0 (d) listesindeki 10 site), `roadmap.md`.

- [ ] **Step 1: Dönüşüm** — 10 sitede:

```cpp
// ÖNCE:
if (!readFile(path, source))
    GTEST_SKIP() << "Integration test file not found: " << path;
// SONRA:
if (!readFile(path, source))
    FAIL() << "Required test fixture missing: " << path
           << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";
```

(`FAIL()` void-dönüşlü gövdede erken döner; TEST_F gövdeleri void — uygunluğunu derleyerek doğrula.)
- [ ] **Step 2: Hard-fail kanıtı** — BİR fixture'ı geçici yeniden adlandır (`git mv` DEĞİL — düz `mv tests/integration/arithmetic.liva{,.bak}`), integration_test'in ilgili testinin FAIL ettiğini gör (skip değil), geri al (`mv` geri), testin PASS ettiğini gör. Çıktılar rapora.
- [ ] **Step 3: roadmap.md** — öncelik tablosu satır 8'e ` — **tamamlandı (2026-07, LIVA_WERROR + fixture hard-fail)**` ekle.
- [ ] **Step 4: Tam seri ctest 2489 yeşil (skip sayısı hâlâ 3 — yalnız canlı testler) + commit** — `test(integration): eksik fixture artık hard-fail — roadmap #8 tamam` + trailer'lar.

---

## Plan öz-inceleme notları

- Spec kapsama: opsiyon→T1/S1, temizlik→T1/S3-4, CI→T1/S5, hard-fail→T2/S1-2, roadmap→T2/S3. Boşluk yok.
- Riskler: (1) build-clang cache'ini bozmamak — Task 0 (b) ayrı dizin stratejisi; (2) MinGW build'in makinede kurulabilirliği — Task 0 (c) önden dener, kurulamıyorsa BLOCKED raporlar ve kapsam Clang+CI'a daralır (controller kararı); (3) `FAIL()` makrosunun yalnız void fonksiyonlarda kullanılabilmesi — TEST_F gövdeleri void, Step 1 derlemeyle doğrular.
