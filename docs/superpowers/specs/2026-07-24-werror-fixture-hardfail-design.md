# CI -Werror + Fixture-Skip Hard-Fail — Tasarım (Roadmap #8)

Tarih: 2026-07-24
Kapsam: `LIVA_WERROR` CMake opsiyonu + iki yerel derleyicide sıfır-uyarı temizliği + `ci.yml` 3 job'da opsiyonun açılması + IntegrationTest'in 10 dosya-bulunamadı skip'inin hard-fail yapılması + roadmap satır 8 kapanışı.

## Keşif özeti

- CI (`.github/workflows/ci.yml`): 3 job — Windows MinGW GCC 13.2, Ubuntu GCC 13/14, macOS AppleClang; LLVM'siz derleme, 10 test hedefi, `-Werror` yok.
- `IntegrationTest.cpp`: 10 adet `if (!readFile(path, source)) GTEST_SKIP()` — eksik fixture sessiz skip (134, 146, 179, 191, 207, 233, 257, 283, 2337 satırları civarı). `RuntimeExecTest.cpp`'nin 3 skip'i (1645 PG conn, 2117/2156 canlı ağ) env-kapılı ve MEŞRU — korunur.
- Yerel derleyiciler: Clang 21 (`build-clang`, clang-cl MSVC frontend) az sayıda uyarı üretiyor; MinGW GCC 15.2 test-only build destekli.

## Değişiklikler

### 1. `LIVA_WERROR` opsiyonu (kök `CMakeLists.txt`)

`option(LIVA_WERROR "Treat warnings as errors" OFF)`. Açıkken: GCC/Clang(GNU frontend) → `-Werror`; clang-cl/MSVC frontend → `/WX`. Mevcut uyarı bayrağı mekanizmasının yanına, YALNIZ proje hedeflerine (üçüncü-parti `_deps`/GoogleTest hariç — mevcut yapı hedef-bazlı mı global mi keşifte netleşir; globalse `_deps`'i dışarıda bırakan hedef-bazlı uygulamaya geçilir).

### 2. Sıfır-uyarı temizliği (iki yerel derleyici)

`-DLIVA_WERROR=ON` ile tam derleme: (a) Clang 21 build-clang, (b) MinGW GCC 15.2 test build'i. Çıkan TÜM uyarılar gerçek düzeltmeyle giderilir (bastırma pragma'sı son çare + gerekçeli; MSVC CRT deprecation gürültüsü için mevcut `_CRT_SECURE_NO_WARNINGS` yaklaşımı korunur). Her iki derleme sıfır-uyarı olana dek.

### 3. CI

`ci.yml` 3 job'ın Configure adımına `-DLIVA_WERROR=ON`. Bilinçli risk (dokümante): CI'ın GCC 13.2/AppleClang'ı yerelde yok; ekstra uyarı bulurlarsa CI kırmızı olur ve ayrı düzeltilir.

### 4. Fixture-skip hard-fail

`IntegrationTest.cpp`'deki 10 dosya-bulunamadı `GTEST_SKIP()` → `FAIL() << "fixture missing: " << path` (test gövdesinden erken dönüşle). Env-kapılı canlı-test skip'leri DEĞİŞMEZ. Kanıt: bir fixture geçici yeniden adlandırılıp FAIL doğrulanır, geri alınır (tek seferlik manuel kanıt, kalıcı test değil).

### 5. Roadmap

Öncelik tablosu satır 8 → tamamlandı (2026-07) işareti.

## Doğrulama

Adım 2 sonrası ve dal sonunda tam seri ctest (taban 2489, sayı değişmemeli — davranış değişikliği yok); MinGW test build'inin derlendiğinin kanıtı; `LIVA_WERROR=OFF` default'unun normal geliştirmeyi etkilemediği (opsiyonsuz configure değişmez).

## Kapsam dışı

CI'a LLVM'li test hedefleri eklemek; macOS/GCC-13 uyarılarını önden tahmin etmek; CI paralel-ctest düzenini değiştirmek.
