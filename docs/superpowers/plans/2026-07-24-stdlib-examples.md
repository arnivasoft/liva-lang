# Stdlib Örnekleri + ExamplesTest Kapısı — Implementasyon Planı (Roadmap #7)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `examples/`'a 11 yeni stdlib örneği + her birini kalıcı doğrulayan `tests/unit/ExamplesTest.cpp` + `examples/README.md` + roadmap 3.4 düzeltmesi. Spec: `docs/superpowers/specs/2026-07-24-stdlib-examples-design.md`.

**Architecture:** Yeni gtest dosyası örnekleri DİSKTEN derler (kaynak-string değil); 9 offline örnek çalıştırılıp stdout'u birebir pinlenir, 2 ağ örneği yalnız derlenir. Örnekler önce livac ile elle doğrulanır, beklenen çıktı GERÇEK çalıştırmadan alınıp teste pinlenir (tahmin yasak).

**Tech Stack:** Liva stdlib modülleri, GoogleTest, livac CLI.

## Global Constraints

- Branch: `feat/stdlib-examples` (main'den).
- TDD: her task'ta test önce RED (örnek dosya yokken), örnek eklenince GREEN. Beklenen stdout literal'i YALNIZ doğrulanmış livac çalıştırmasının çıktısından kopyalanır.
- Her task sonunda: `cmake --build build-clang` + TAM seri ctest (`ctest --test-dir build-clang --output-on-failure`, ASLA `-j`, FOREGROUND, timeout 600000ms). Taban: **2474 test yeşil** (3 bilinen opt-in skip).
- Commit trailer'ları zorunlu:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216`
- Örnek dosya kuralları (spec'ten): tek dosya ~30-70 satır; başta İngilizce yorum bloğu (ne gösterdiği + çalıştırma talimatı `livac json_demo.liva -o json_demo && ./json_demo` biçiminde; ağ örneklerinde önkoşul notu); deterministik kısa stdout; yorumlar İngilizce.
- Bilinen dil tuzakları (roadmap 2.3 — İHLAL ETME): top-level `var`/`let` YOK (livac segfault!); çağrı-sonucu doğrudan member zinciri YOK (ara `let` binding); `strSplit`/dizi binding'leri açık `[T]` anotasyonlu; generik argümansız statik YOK; untyped main'de çıplak `return` YOK; struct alanında `pub` YOK; Liva yorumları `//`.
- livac tek-dosya modu: `F:\Cpp_Projects\liva-lang\build-clang\livac.exe <file> -o <out>`; örnek geliştirirken scratchpad kullan, bitmiş dosyayı `examples/`'a koy ve ORADAN tekrar doğrula.
- Sapma protokolü: bir modülün API'si örneklenemeyecek kadar kırıksa (derlenmeyen/yanlış çalışan) örneği daraltıp raporla; hiç yazılamıyorsa BLOCKED + roadmap izleme notu öner.

---

### Task 0: Keşif haritası (salt-okunur, commit yok)

Rapor → `.superpowers/sdd/task-0-report.md`, her iddia file:line veya probe çıktısı kanıtlı:

- [ ] (a) Test altyapısı: `tests/unit/ProjectConfigTest.cpp`'nin `projectRoot()` kalıbı (root + "/examples/hello.liva" kullanımı, ~1690); RuntimeExecTest'in derle+çalıştır yardımcısının iç yapısı (livac yolunu nereden buluyor, exit code + stdout yakalama nasıl); `tests/CMakeLists.txt`'e yeni test dosyası ekleme kalıbı (~190 add_executable döngüsü).
- [ ] (b) Her modül için API özet çıkarımı — örnek yazacak task'ların TEK API kaynağı bu rapor olacak. Modül başına: pub tip/fonksiyon imzaları (stdlib/<mod>/<mod>.liva'dan) + çalışan BİR kullanım örneği (tests/unit içindeki mevcut testlerden, file:line): `json::json`, `sqlite::sqlite`, `regex::regex`, `crypto::crypto`, `jwt::jwt`, `csv::csv`, `toml::toml`, `time::time`, `http::http`, `websocket::websocket`, `cli::cli`.
- [ ] (c) Probe: en az 3 modülde (json, regex, time) 10-15 satırlık mini program derle+çalıştır (scratchpad) — API özetinin doğruluğunu kanıtla; sqlite `:memory:` bağlantı biçimini probe'la doğrula.
- [ ] (d) http/websocket örneklerinin YALNIZ DERLEME hedefi için: bu modüllerle ağsız derlemenin gerçekten geçtiğini probe'la doğrula (derle ama çalıştırma).
- [ ] (e) `examples/README.md` için mevcut 97 dosyanın tek satırlık kategorize listesi (Language/Stdlib/UI) — dosya adlarından + gerekiyorsa ilk yorum satırından.

### Task 1: ExamplesTest altyapısı + map_set_demo (TDD)

**Files:**
- Create: `tests/unit/ExamplesTest.cpp`, `examples/map_set_demo.liva`
- Modify: `tests/CMakeLists.txt`

**Interfaces (sonraki task'lar bunlara güvenir — imzalar birebir):**
- `static RunResult compileAndRunExample(const std::string &baseName)` — `examples/<baseName>.liva`'yı livac ile derler, çalıştırır; `{exit_code, stdout_output}` döner; derleme hatasında exit_code != 0 + stderr raporda.
- `static bool compileExampleOnly(const std::string &baseName)` — yalnız derleme başarısı.
- Test adlandırma: `TEST(ExamplesTest, MapSetDemo)` biçimi.

- [ ] **Step 1: RED** — `ExamplesTest.cpp`'yi Task 0 (a) kalıplarıyla yaz (projectRoot + livac çağrısı + çıktı yakalama; RuntimeExecTest yardımcının uyarlaması) ve ilk testi ekle:

```cpp
TEST(ExamplesTest, MapSetDemo) {
    auto r = compileAndRunExample("map_set_demo");
    EXPECT_EQ(r.exit_code, 0) << r.stdout_output;
    EXPECT_EQ(r.stdout_output, /* doğrulanmış çalıştırmadan pinlenecek */ "...") ;
}
```

`tests/CMakeLists.txt`'e kaydet, derle, çalıştır → RED (dosya yok).
- [ ] **Step 2: `examples/map_set_demo.liva` yaz** — built-in Map/Set tam yüzeyi: `var m: Map<string, i32>` insert/get(if-let)/contains/remove/size/isEmpty/`for (k, v)`/keys()/values()/clear(); `var s: Set<i64>` insert/contains/size. NOT: `for (k,v)` hash sırası belirsiz — deterministik çıktı için iterasyonda TOPLAM/SAYAÇ bas, tek tek çift basma. Scratchpad'de doğrula, `examples/`'a koy, ORADAN tekrar derle+çalıştır, çıktıyı teste pinle.
- [ ] **Step 3: GREEN + tam suite + commit** — `feat(examples): ExamplesTest kapısı + map_set_demo` + trailer'lar.

### Task 2: Veri biçimleri — json, csv, toml (TDD)

**Files:** Create: `examples/json_demo.liva`, `examples/csv_demo.liva`, `examples/toml_demo.liva`; Modify: `tests/unit/ExamplesTest.cpp`

- [ ] **Step 1: RED** — 3 test ekle (`JsonDemo`, `CsvDemo`, `TomlDemo`, compileAndRunExample ile) → dosyalar yokken FAIL.
- [ ] **Step 2: Örnekleri yaz** (Task 0 (b) API raporuna sadık; her biri scratchpad'de doğrula → examples/'a koy → tekrar doğrula → çıktıyı pinle):
  - `json_demo.liva`: sabit JSON string parse → nesne alanı, iç içe dizi gezinme, sayı/bool/string okuma → programatik JSON kurup stringe çevirme. (Hafıza notu: DOM sahiplik — parse sonucunu BINDING'e al, geçicilerden zincirleme yapma.)
  - `csv_demo.liva`: çok satırlı CSV string parse → satır sayısı, alan erişimi → yeni CSV üretme.
  - `toml_demo.liva`: config string parse → bölüm/anahtar okuma (string, sayı, bool).
- [ ] **Step 3: GREEN + tam suite + commit** — `feat(examples): json/csv/toml örnekleri` + trailer'lar.

### Task 3: sqlite, regex, crypto+jwt (TDD)

**Files:** Create: `examples/sqlite_demo.liva`, `examples/regex_demo.liva`, `examples/crypto_jwt_demo.liva`; Modify: `tests/unit/ExamplesTest.cpp`

- [ ] **Step 1: RED** — `SqliteDemo`, `RegexDemo`, `CryptoJwtDemo` testleri.
- [ ] **Step 2: Örnekler**:
  - `sqlite_demo.liva`: `:memory:` aç → CREATE TABLE → birkaç INSERT → SELECT ile gezip kolonları tip'li erişimcilerle oku → toplam/satır bas → kapat.
  - `regex_demo.liva`: isMatch, find, findAll (sayısı + elemanlar), replace, grup yakalama.
  - `crypto_jwt_demo.liva`: sha256 hex, HMAC-SHA256 hex, JWT sign (sabit payload+secret) → verify (true) → bozuk token verify (false). NOT: JWT çıktısı deterministikse token'ın kendisi basılabilir; değilse yalnız verify sonuçları basılır — çalıştırmada gör, ona göre pinle.
- [ ] **Step 3: GREEN + tam suite + commit** — `feat(examples): sqlite/regex/crypto+jwt örnekleri` + trailer'lar.

### Task 4: time, cli (TDD)

**Files:** Create: `examples/time_demo.liva`, `examples/cli_demo.liva`; Modify: `tests/unit/ExamplesTest.cpp`

- [ ] **Step 1: RED** — `TimeDemo`, `CliDemo` testleri.
- [ ] **Step 2: Örnekler**:
  - `time_demo.liva`: SABİT tarihlerle Date/Time/DateTime kur (now() BASMA — deterministik değil; now() kullanılacaksa yalnız türetilmiş sabit ilişkiler bas), aritmetik (gün ekleme, fark), biçimleme.
  - `cli_demo.liva`: COOKBOOK §26'daki kalıbın kısaltılmışı; `os.getArgs()` yerine SABİT `[string]` arg dizisiyle iki parse senaryosu (başarılı + hatalı) + usage() basımı — deterministik.
- [ ] **Step 3: GREEN + tam suite + commit** — `feat(examples): time/cli örnekleri` + trailer'lar.

### Task 5: Ağ örnekleri (yalnız derleme) + README + roadmap kapanışı

**Files:** Create: `examples/http_demo.liva`, `examples/websocket_demo.liva`, `examples/README.md`; Modify: `tests/unit/ExamplesTest.cpp`, `roadmap.md`

- [ ] **Step 1: RED** — `HttpDemoCompiles`, `WebsocketDemoCompiles` (compileExampleOnly ile) testleri.
- [ ] **Step 2: Ağ örnekleri** (derleme-doğrulamalı; ÇALIŞTIRILMAZ — dosya başı yorumda "requires network access" + gerçek çalıştırma talimatı):
  - `http_demo.liva`: `https://httpbin.org/json` GET → durum kodu, header, gövdeyi json parse edip alan okuma.
  - `websocket_demo.liva`: echo sunucuya bağlan → text gönder → yanıt al → kapat (hafıza notu: keepalive/Drop kalıbı websocket-redesign'a uygun).
- [ ] **Step 3: `examples/README.md`** — Task 0 (e) listesinden kategorili indeks (Language Features / Stdlib / UI); 11 yeni örnek "NEW (2026-07)" işaretli; Önkoşullar bölümü (UI: wx DLL'leri, ağ örnekleri: internet).
- [ ] **Step 4: roadmap.md** — 3.4: artifact cümlesini gerçekle değiştir ("artifact'lar zaten gitignore'lu ve izlenmiyor; yerel wx DLL'leri UI demo'larının çalışması için gerekli"), örnek boşluğunu "tamamlandı (2026-07, 11 örnek + ExamplesTest)" işaretle; öncelik tablosu satır 7 tamamlandı.
- [ ] **Step 5: GREEN + tam suite + commit** — `feat(examples): http/websocket örnekleri + README + roadmap #7 tamam` + trailer'lar.

---

## Plan öz-inceleme notları

- Spec kapsama: 11 örnek → Task 1-5; ExamplesTest → Task 1 (+ her task genişletir); README → Task 5; roadmap düzeltmesi → Task 5. Boşluk yok.
- Tip tutarlılığı: `compileAndRunExample(baseName)` / `compileExampleOnly(baseName)` imzaları Task 1'de tanımlı, 2-5 aynen kullanır.
- Bilinen risk: (1) modül API'leri örnek yazarken sürprizli çıkabilir — Task 0 (b/c) önden haritalar, sapma protokolü var; (2) JWT/hash çıktı determinizmi — Task 3 çalıştırma-önce-pinleme kuralıyla çözülür; (3) ağ örneklerinin derlemesi ağsız geçmeli — Task 0 (d) önden doğrular.
