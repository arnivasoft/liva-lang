# Generic Map Tamamlama + CLI Arg Parser — Tasarım (Roadmap #6)

Tarih: 2026-07-24
Kapsam: Tek branch, iki faz. Faz A compiler built-in `Map<K,V>`/`Set<T>` tamamlama + insert-coercion hata düzeltmesi; Faz B saf-Liva `cli::cli` stdlib modülü.

## Keşif özeti (tasarımın dayandığı doğrulanmış durum)

- Built-in `Map<K,V>` mevcut: `var m: Map<string, i32>` bare-anotasyon bildirimi, `insert/get/contains/remove` (`IRGenCallMethod.cpp:871-1090`), `for (k, v) in m` iterasyonu (`IRGenStmt.cpp:1150-1230`), runtime open-addressing tablo (`runtime.cpp:1255+`, entry = `{state u8, hash u64, key, val}`, stride `9+keySize+valSize`). `Set<T>` aynı struct'ı kullanır.
- **Doğrulanmış hata**: `var m: Map<string, i64>` + `m.insert("a", 1)` → `get` çöp döner (`140694538682369` = düşük word 1, yüksek 32 bit çöp). Kök neden: i32 literal, i64 tipli `map.val.tmp` alloca'sına widening'siz store ediliyor; memcpy 8 bayt kopyalayınca üst 4 bayt tanımsız. Aynı sınıf `Map<i64, V>` anahtarları ve `Set<i64>` elemanları için de geçerli.
- Eksik metodlar: `size/isEmpty/clear/keys/values` (size alanı struct'ta zaten tutuluyor).
- CLI zemini: `os.getArgs() -> [String]`, `string ==/!=`, `[string]` for-in, `substring`, `s[a..b]` slicing, `strStartsWith/strSplit`, `parseInt` — hepsi probe-doğrulamalı çalışıyor.
- Bilinen kısıtlar (tasarım bunlara uyar): struct alanında `Map` desteklenmiyor (paralel diziler kullanılır); argümansız GENERİK statik `T` çözemez ama `ArgParser` non-generik olduğundan `ArgParser.new(...)` sorunsuz; `mut self`'ten `return self` yok (fluent builder kullanılmaz); int literal'ler katı i32.

## Faz A — Built-in Map/Set tamamlama

### A1. Insert/lookup i32→i64 coercion (hata düzeltmesi, TDD)

`IRGenCallMethod.cpp` Map/Set built-in yollarında, anahtar ve değer argümanları ilgili beklenen LLVM tipine (info.keyType/valType/elemType) karşı kontrol edilir: gelen değer i32, beklenen i64 ise sign-extend (mevcut `toI64` yardımcısı / signedness-aware kalıp). Kapsanan yollar: Map `insert/get/contains/remove` (anahtar + insert değeri), Set `insert/contains/remove`. RED kanıtı: yukarıdaki çöp-değer repro'su.

### A2. Yeni metodlar

| Metod | Dönüş | Mekanizma |
|-------|-------|-----------|
| `m.size()` / `s.size()` | `i64` | struct alan 1 load |
| `m.isEmpty()` / `s.isEmpty()` | `bool` | `size == 0` |
| `m.clear()` / `s.clear()` | void | yeni runtime `liva_map_clear(void *entries, int64_t cap, int64_t stride, int64_t *size)` — buffer memset(0) + `*size = 0` (`RuntimeFunctions.def`'e bir satır; tombstone'lar da temizlenir) |
| `m.keys()` | `[K]` | IRGen inline döngü (for-in iterasyon kalıbı): occupied entry'lerden anahtarı yeni DynArray'e `liva_array_push` |
| `m.values()` | `[V]` | aynı kalıp, değer alanından |

Sema (`TypeCheckerCall.cpp` Map/Set Generic dalı): `size -> i64`, `isEmpty -> bool`, `keys -> [K]`, `values -> [V]` resolved-type kayıtları; `clear` void.

Not: `keys()/values()` dönen DynArray'in sahipliği çağırana geçer (mevcut DynArray scope-cleanup mekanizması serbest bırakır). String anahtar/değerlerde dizi elemanları ham pointer kopyasıdır (mevcut dizi semantiğiyle tutarlı; string içerik sahipliği map'te kalır — kullanıcı map'i clear/serbest bıraktıktan sonra dizi elemanına erişmemelidir; dokümante edilir).

### A3. Kapsam dışı (roadmap izleme satırları)

Map'in fonksiyon parametresi/dönüş değeri/struct alanı olması; `Map.new()` sözdizimi; Set `toArray()`.

## Faz B — `cli::cli` modülü (`stdlib/cli/cli.liva`, saf Liva)

### API

```liva
pub struct ArgParser { /* name, description + paralel spec dizileri */ }
pub struct ParseResult { pub var ok: bool  pub var error: string  pub var helpRequested: bool  /* + iç eşleşme dizileri */ }

impl ArgParser {
    pub func new(name: string, description: string) -> ArgParser
    pub func addFlag(ref mut self, long: string, short: string, help: string)
    pub func addOption(ref mut self, long: string, short: string, defaultVal: string, help: string)
    pub func addPositional(ref mut self, name: string, help: string)
    pub func parse(ref self, args: [string]) -> ParseResult
    pub func usage(ref self) -> string
}

impl ParseResult {
    pub func getFlag(ref self, long: string) -> bool
    pub func getOption(ref self, long: string) -> string      // default uygulanmış
    pub func positionalCount(ref self) -> i64
    pub func getPositional(ref self, i: i64) -> string?       // aralık dışı → nil
}
```

### Ayrıştırma kuralları

- `--long` → flag set; `--long=val` ve `--long val` → option değeri; `-x` kısa ad eşlemesi (bundling YOK).
- `--` ayracı: sonraki her şey positional.
- `--help` / `-h`: `helpRequested=true`, ayrıştırma normal biter (`ok=true`).
- Hatalar (`ok=false`, `error` açıklayıcı; ilk hatada durur): bilinmeyen `--x`/-x`; option'a değer verilmemiş (arg listesi bitti veya sonraki arg `-` ile başlıyor); bildirilen positional eksik. Fazla positional serbesttir (listeye eklenir).
- Aynı flag/option tekrarı: son değer kazanır (hata değil).
- `usage()`: `Usage: <name> [options] <positionals...>` + açıklama + hizalı option/flag listesi (`-v, --verbose  Help metni`), default değerler gösterilir.

### İç temsil

Struct alanında Map olmadığından paralel diziler: spec tarafında `flagLongs/flagShorts/flagHelps: [string]`, option karşılıkları + `optionDefaults`, `positionalNames/positionalHelps`; sonuç tarafında `setFlags: [string]`, `optionNames/optionValues: [string]`, `positionals: [string]`. Aramalar lineer tarama (CLI ölçeğinde yeterli).

### Test ve doğrulama

- TDD, RuntimeExec `compileAndRun` + `import cli::cli` (sqlite/json test kalıbı). Asgari: flag uzun/kısa, option `=`/ayrık/default, positional zorunluluk hatası, bilinmeyen-arg hatası, değersiz-option hatası, `--` ayracı, `--help`, usage içerik kontrolü, tekrar-son-değer.
- Faz A: coercion repro RED→GREEN, size/isEmpty/clear/keys/values Map(string ve i64 anahtar) + Set testleri, keys/values boş map'te boş dizi.
- Her commit'te tam seri ctest yeşil (`ctest --test-dir build-clang`, ASLA `-j`).

### Dokümantasyon

LANGUAGE-REFERENCE (EN+TR): Map/Set metod tablosu güncelle; COOKBOOK (EN+TR): kısa CLI tarifi (livac-doğrulamalı). roadmap 3.1 satır 1-2 tamamlandı işaretle + A3 izleme satırları.
