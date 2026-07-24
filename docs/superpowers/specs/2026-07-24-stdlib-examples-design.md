# Stdlib Örnekleri + Test Kapısı — Tasarım (Roadmap #7)

Tarih: 2026-07-24
Kapsam: `examples/`'a 11 yeni stdlib örneği + bunları kalıcı doğrulayan `ExamplesTest` gtest'i + `examples/README.md` indeksi + roadmap 3.4 düzeltmesi.

## Keşif özeti (tasarımın dayandığı doğrulanmış durum)

- `examples/`'ta 97 dosya var; stdlib import kullanan yalnız `db_unified_demo.liva` (db::db) ve UI örnekleri. http/websocket/json/sqlite/regex/crypto/jwt/csv/toml/time/cli/Map-Set örneksiz.
- Roadmap 3.4'ün "commit edilmiş .exe/.dll artifact'ları gitignore'a alınmalı" iddiası BAYAT: kök `.gitignore` `*.exe`/`*.dll`'i zaten kapsıyor, `git ls-files examples`'ta izlenen artifact 0. Yerel wx DLL'leri UI demo'larını çalıştırmak için elle kopyalanmış — SİLİNMEZ.
- Örnekleri derleyen sistematik test yok (yalnız 2 tesadüfi referans: IntegrationTest classes.liva, ProjectConfigTest hello.liva) — drift riskinin kaynağı.

## Yeni örnekler (11 dosya, `examples/`)

Her dosya: tek dosya, ~30-70 satır, başta İngilizce yorum bloğu (ne gösterdiği + `livac <file> -o <out> && ./<out>` çalıştırma talimatı; ağ örneklerinde ek önkoşul notu). Tüm örnekler YAZILMADAN ÖNCE livac ile derlenip (offline olanlar çalıştırılıp) doğrulanır.

| Dosya | İçerik | Doğrulama |
|-------|--------|-----------|
| `json_demo.liva` | `import json::json` — parse → alan erişimi/dizi gezinme → JSON üretme | derle+çalıştır+çıktı |
| `sqlite_demo.liva` | `import sqlite::sqlite` — in-memory `:memory:` DB: CREATE, INSERT, SELECT, tip'li kolon erişimcileri | derle+çalıştır+çıktı |
| `regex_demo.liva` | `import regex::regex` — isMatch, find, findAll, replace, gruplar | derle+çalıştır+çıktı |
| `crypto_jwt_demo.liva` | `import crypto::crypto` + `import jwt::jwt` — sha256 hash, HMAC, JWT sign/verify | derle+çalıştır+çıktı |
| `csv_demo.liva` | `import csv::csv` — string'den parse, satır/alan erişimi, CSV üretme | derle+çalıştır+çıktı |
| `toml_demo.liva` | `import toml::toml` — config string'i parse, değer okuma | derle+çalıştır+çıktı |
| `time_demo.liva` | `import time::time` — Date/Time/DateTime kurma, aritmetik, biçimleme | derle+çalıştır+çıktı |
| `cli_demo.liva` | `import cli::cli` — ArgParser flag/option/positional/usage; sabit arg dizisiyle demo (getArgs değil — deterministik çıktı için) | derle+çalıştır+çıktı |
| `map_set_demo.liva` | built-in Map/Set: insert/get/contains/remove/size/isEmpty/clear/keys/values + `for (k,v)` | derle+çalıştır+çıktı |
| `http_demo.liva` | `import http::http` — GET + durum kodu + gövde JSON parse (httpbin.org); ağ gerekir | YALNIZ derleme |
| `websocket_demo.liva` | `import websocket::websocket` — bağlan/gönder/al/kapat (echo.websocket.org tarzı echo); ağ gerekir | YALNIZ derleme |

Örnek içerik kuralları:
- Bilinen dil tuzaklarına uyulur (roadmap 2.3 / hafıza): top-level `var` YOK (her şey main/fonksiyon içinde), çağrı-sonucu doğrudan member zinciri YOK (ara `let` binding), `strSplit`/dizi binding'leri açık `[T]` anotasyonlu, generik argümansız statik YOK.
- Deterministik çıktı: offline örneklerin stdout'u sabittir (timestamp basmak yerine bileşen/fark değerleri basılır; time_demo sabit tarihlerle kurulur).
- Çıktılar kısa tutulur (test pinlemesi okunaklı kalsın).

## Test kapısı: `tests/unit/ExamplesTest.cpp`

- Yeni gtest dosyası; `tests/CMakeLists.txt`'e mevcut kalıpla eklenir.
- Mevcut RuntimeExec altyapısındaki derle+çalıştır yardımcının örnek-dosyası varyantı: `examples/<f>.liva` yolunu proje kökünden çözer (ProjectConfigTest'in `projectRoot()` kalıbı), livac ile derler.
- 9 offline örnek: derle + çalıştır + stdout birebir EXPECT_EQ.
- 2 ağ örneği: yalnız derleme başarısı (çalıştırılmaz).
- Mevcut 97 dosya kapsam DIŞI — bu test yalnız yeni örnekleri bilir (açık liste, glob yok).

## README: `examples/README.md`

Kategorili indeks: Language Features / Stdlib / UI. Tüm mevcut .liva dosyaları birer satırla listelenir (kısa açıklama), yeni 11 örnek "NEW (2026-07)" işaretli. Çalıştırma önkoşulları bölümü: UI örnekleri için wx DLL'leri notu, ağ örnekleri notu.

## Roadmap düzeltmesi

3.4: artifact cümlesi düzeltilir ("artifact'lar zaten gitignore'lu ve izlenmiyor; yerel wx DLL'leri UI demo'ları için gerekli" gerçeğiyle); örnek boşluğu kısmı "tamamlandı (2026-07, 11 örnek + ExamplesTest kapısı)" işaretlenir. Öncelik tablosu satır 7 tamamlandı işaretlenir.

## Doğrulama

TDD: ExamplesTest önce RED (örnek dosyalar yokken derleme hatası), örnekler eklendikçe GREEN. Her commit'te tam seri ctest (taban 2474 + yeniler). Ağ örnekleri derleme-testi lokal ağsız da geçer.

## Kapsam dışı

Mevcut 97 örneğin doğrulanması/düzeltilmesi; stream/sync/log örnekleri (concurrency_demo zaten var; stream/log sonraki tur); örneklerin CI matrisine ayrı iş olarak bağlanması (ExamplesTest zaten ctest'e girer).
