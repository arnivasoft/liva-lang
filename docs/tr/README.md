# Liva Programlama Dili

Swift benzeri sözdizimine ve Rust tarzı ownership/borrowing semantiğine sahip, statik tipli bir programlama dili.

Liva, LLVM aracılığıyla native koda derlenir; garbage collector olmadan bellek güvenliği, ifade gücü yüksek pattern matching, trait bound'lu generics ve büyüyen bir standart kütüphane sunar.

## Özellikler

- **Ownership & Borrowing** — Move semantics, referanslar (`ref`, `ref mut`), lifetime analizi
- **Tip Sistemi** — Generics, protocol'ler (trait'ler), optional tipler, result tipleri, type alias'lar
- **Pattern Matching** — Kapsamlı `match` ifadeleri, iç içe pattern'ler, enum associated value'lar
- **Closure'lar** — Value/reference ile yakalama, trailing closure sözdizimi, tip çıkarımı
- **Async/Await** — Coroutine tabanlı asenkron programlama
- **Modüller** — Standart kütüphane modülleriyle import sistemi
- **LLVM Backend** — Optimizasyon seviyeleriyle (O0-O3) native kod üretimi
- **Araçlar** — LSP sunucusu (18+ özellik), JIT destekli interaktif REPL, `livac bench`, `livac test`, proje manifest'i (`liva.toml`)
- **Sınıflar (Classes)** — Swift tarzı sınıf sistemi: tekli kalıtım, vtable dispatch, `init`/`deinit`, `override`, `super`, `final`, `static`, hesaplanmış özellikler, özellik gözlemcileri (`willSet`/`didSet`), `is`/`as?` tip kontrolleri, failable `init?`, aşırı yüklemeli `convenience init`, `lazy var`, subscript (generic dahil), beş erişim seviyesi (`open`/`public`/`internal`/`fileprivate`/`private`), extension
- **FFI** — C uyumluluğu için `extern "C"` bildirimleri
- **Comptime & Macro'lar** — Derleme zamanı değerlendirme blokları, kalıp tabanlı macro'lar
- **dyn Protocol** — Dinamik dispatch'li trait nesneleri
- **Test Framework** — Dahili `test` blokları ve `livac test`
- **Eşzamanlılık** — Channel'lar (tamponlu), yapısal eşzamanlılık için TaskGroup'lar
- **Benchmarking** — `livac bench` ile dahili performans testi
- **JIT Derleme** — LLJIT tabanlı anında derleme
- **Çapraz Derleme** — Çoklu platform desteği için `--target` bayrağı
- **Plugin Sistemi** — Özel analiz için derleyici plugin API'si
- **WASM** — WebAssembly hedef desteği (`--target wasm32`)
- **UI Framework** — raylib tabanlı widget sistemi (12 faz: widget'lar, layout, tema, animasyon, odak, tooltip)
- **Zengin Tanılama** — Rust tarzı alt çizgi span'ları, yardım önerileri, "bunu mu demek istediniz" önerileri
- **Debug Adapter** — Koşullu breakpoint, ifade değerlendiricili DAP sunucusu
- **Ayrı Derleme** — Artımlı iş akışları için `--emit-obj` ve `livac link`
- **Güvenlik** — Dilim sınır kontrolü, ayrıştırma taşma korumaları, FFI tip güvenliği uyarıları
- **Async Runtime** — İş parçacığı havuzu zamanlayıcısı, channel'lar, task grupları, `for await`, async I/O

## Hızlı Başlangıç

```liva
func fibonacci(n: i32) -> i32 {
    if n <= 1 {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}

func main() {
    for i in 0..10 {
        println(fibonacci(i))
    }
}
```

## Kaynaktan Derleme

### Ön Gereksinimler

- **CMake** 3.20+
- **Ninja** build sistemi
- **C++20** derleyici (Clang 16+, GCC 13+ veya MSVC 2022)
- **LLVM 21** (opsiyonel — kod üretimi için gerekli, testler için değil)
- **GoogleTest** (CMake tarafından otomatik indirilir)

### Windows (Önerilen: Clang + MSVC ABI)

```batch
:: C:\LLVM'de LLVM/Clang ve Visual Studio kurulu olmalı
build_clang.bat
ctest --test-dir build-clang --output-on-failure
```

### Windows (MinGW — sadece testler, codegen yok)

```batch
cmake -G "MinGW Makefiles" -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### Linux / macOS

```bash
# Ninja gerekli: apt install ninja-build / brew install ninja
./build.sh --test

# Veya manuel olarak:
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Kullanım

```bash
# Tek dosya derleme
livac -o hello examples/hello.liva

# Derle ve çalıştır
livac run

# Proje derlemesi (liva.toml okur)
livac build --release

# Yeni proje oluştur
livac init myproject

# LSP sunucusunu başlat (editör entegrasyonu için)
livac lsp

# İnteraktif REPL
livac repl

# Benchmark çalıştır
livac bench

# Testleri çalıştır
livac test

# Bağımlılık kaldır
livac remove <paket>

# Çapraz derleme
livac build --target x86_64-linux-gnu

# Tanılama araçları
livac --dump-tokens file.liva    # Token akışını göster
livac --dump-ast file.liva       # AST'yi göster
livac --check-only file.liva     # Codegen olmadan tip kontrolü
livac --emit-ir file.liva        # LLVM IR çıktısı
livac --emit-obj file.liva      # Obje dosyası çıktısı
livac --dump-timings file.liva  # Faz bazlı derleme zamanlamasını göster
livac --trace-macros file.liva  # Macro genişletmelerini izle
livac link a.o b.o -o app       # Obje dosyalarını linkle
livac format file.liva          # Kaynak kodu biçimlendir
livac lint file.liva            # Linter çalıştır
livac dap                       # Debug Adapter Protocol sunucusunu başlat
```

## Dile Genel Bakış

### Değişkenler ve Sabitler

```liva
let name: string = "Liva"      // değişmez bağlama
var count: i32 = 0              // değişebilir bağlama
const MAX_SIZE: i32 = 100       // derleme zamanı sabiti
```

### Fonksiyonlar

```liva
func add(a: i32, b: i32) -> i32 {
    return a + b
}

func greet(name: string, greeting: string = "Hello") {
    println("\(greeting), \(name)!")
}
```

### Struct'lar ve Metodlar

```liva
struct Point {
    var x: f64
    var y: f64
}

impl Point {
    func new(x: f64, y: f64) -> Point {
        return Point { x: x, y: y }
    }

    func distance(ref self) -> f64 {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    func translate(ref mut self, dx: f64, dy: f64) {
        self.x = self.x + dx
        self.y = self.y + dy
    }
}
```

### Enum'lar ve Pattern Matching

```liva
enum Shape {
    case Circle(f64)
    case Rectangle(f64, f64)
    case Triangle(f64, f64, f64)
}

func area(shape: Shape) -> f64 {
    match shape {
        Circle(r) => 3.14159 * r * r
        Rectangle(w, h) => w * h
        Triangle(a, b, c) => {
            let s = (a + b + c) / 2.0
            return sqrt(s * (s - a) * (s - b) * (s - c))
        }
    }
}
```

### Generics ve Protocol'ler

```liva
protocol Printable {
    func toString(ref self) -> string
}

func printItem<T: Printable>(item: ref T) {
    println(item.toString())
}

struct Box<T> {
    var value: T
}

impl<T> Box<T> {
    func get(ref self) -> T {
        return self.value
    }
}
```

### Ownership ve Referanslar

```liva
func take_ownership(s: string) {
    println(s)
    // s burada drop edilir
}

func borrow(s: ref string) {
    println(s)          // salt okunur borrow
}

func mutate(s: ref mut string) {
    s = "modified"      // mutable borrow
}
```

### Closure'lar

```liva
let numbers = [1, 2, 3, 4, 5]
let doubled = numbers.map { |x| x * 2 }
let evens = numbers.filter { |x| x % 2 == 0 }
let sum = numbers.reduce(0) { |acc, x| acc + x }
```

### Hata Yönetimi

```liva
enum FileError {
    case NotFound
    case PermissionDenied
}

func readFile(path: string) -> Result<string, FileError> {
    // ...
}

func main() {
    let result = readFile("data.txt")
    match result {
        Ok(content) => println(content)
        Err(e) => println("Dosya okuma hatası")
    }
}
```

### Optional Tipler

```liva
func find(arr: [i32], target: i32) -> i32? {
    for item in arr {
        if item == target {
            return item
        }
    }
    return nil
}

let value = find([1, 2, 3], 2) ?? 0    // nil coalescing
```

### Async/Await

```liva
async func fetchData(url: string) -> string {
    // Ağ istekleri için http::http kullanın
    let resp = HttpRequest.get(url).send()
    return resp.text()
}
```

### Standart Kütüphane Modülleri

Yerleşik `std::*` modülleri (düşük seviye builtin'ler):

```liva
import std::math      // abs, sqrt, pow, sin, cos, floor, ceil, round, ...
import std::io        // print, println, readLine, File, fileRead/Write, yol yardımcıları
import std::convert   // parseInt, parseInt64, parseFloat, toString, charToString
import std::os        // env, args, exit, exec, processStart/Wait/Kill/Read
import std::random    // randInt, randFloat, randSeed, randI64, randUuid
import std::regex     // regexMatch, regexFind/FindAll, regexSplit, derlenmiş regex
import std::net       // düşük seviyeli HTTP builtin'leri (httpRequestEx, httpStatus, httpBody, …)
import std::json      // jsonParse + DOM düğüm builtin'leri (jsonObjGet, jsonArrAt, jsonToString, ...)
import std::datetime  // dateNow, dateParse, dateAdd, dateDiff, dateFormat
import std::compress  // base64/hex/urlEncode+Decode, crc32
import std::crypto    // sha256, md5, hmacSha256, base64, hex
import std::sync      // mutex, atomic, channel, taskGroup temelleri
import std::async     // schedulerInit, taskSelect, withTimeout, async I/O
import std::collections // Map, Set, forEach, enumerate, zip, sorted, ...
import std::strings   // str* yardımcıları + UTF-8 (strCharCount, strCodepointAt, charIsAlpha, ...)
import std::test      // assert, assertEq, testRunClosure
import std::log       // logDebug/Info/Warn/Error, logSetLevel
import std::ui        // wxWidgets tabanlı UI (widget, layout, tema, canvas)
```

Ergonomik sarmalayıcı modüller (builtin'lerin üzerine struct API'leri):

```liva
import random::random         // Random struct + randBool/randPercent
import os::os                 // Process struct + getEnv/getArgs/runCommand
import log::log               // Tag destekli Logger struct
import math::math             // PI/TAU/E sabitleri, clamp/sign/degToRad
import convert::convert       // toInt/toFloat + toIntOr/toFloatOr
import encoding::encoding     // Base64/Hex/Url struct'ları + toBase64/toHex/toUrl
import errors::errors         // withContext/unwrapOr + ErrorChain
import collections::collections // Stack<T>, Queue<T>, Deque, HashSet + math/slice helper'lar
import strings::strings       // toCodepoints, countAlpha/Digit, isAlnum, isBlank
import io::io                 // LineReader, LineWriter, readLines, writeLines
import time::time             // Duration, Instant, Timer, DateTime (+ add/sub/diff gün/saat)
import fs::fs                 // FileInfo (size, modifiedTime, isDir), Dir işlemleri
import path::path             // Path manipülasyonu
import http::http             // HttpRequest (akıcı oluşturucu), HttpResponse (eager-copy), HttpClient
import net::net               // Url (ayrıştır/oluştur/kodla/çöz)
import json::json             // Json/JsonValue/JsonObject/JsonArray ayrıştırma ağacı (tipli get, path, obj["k"])
import crypto::crypto         // Hash/Hmac struct'ları
import sync::sync             // Mutex, AtomicI64, Channel, TaskGroup
import async::async           // withTaskGroup, withTimeout, raceIndex
import regex::regex           // Regex struct (isMatch, find, findAll, split, groups)
import testing::testing       // TestSuite, TestGroup, Expect/ExpectStr/ExpectFloat
```

## Proje Manifest'i

Proje kök dizininde bir `liva.toml` dosyası oluşturun:

```toml
[project]
name = "myapp"
version = "1.0.0"
entry = "src/main.liva"

[build]
optimization = "release"

[dependencies]
json_parser = "^1.0.0"
```

## Mimari

```
Kaynak Kod (.liva)
        |
        v
    [ Lexer ]  -->  Token Akışı (103 token türü)
        |
        v
    [ Parser ] -->  AST (40+ düğüm türü)
        |
        v
    [ Sema ]   -->  Tip Kontrolü + Ownership Analizi + Lifetime Analizi
        |
        v
    [ IRGen ]  -->  LLVM IR
        |
        v
    [ CodeGen ] --> Native Çalıştırılabilir Dosya
        |
        v
    [ JIT ]    -->  Bellekte Çalıştırma (LLJIT)
```

## Proje Yapısı

```
liva-lang/
  include/liva/       # Genel header dosyaları (AST, Lexer, Parser, Sema, IR, ...)
  src/                 # Uygulama dosyaları
    AST/               # AST düğümleri ve printer
    Lexer/             # Tokenizer
    Parser/            # Recursive-descent parser
    Sema/              # Tip kontrolcüsü, ownership kontrolcüsü, lifetime analizi
    IR/                # LLVM IR üretimi (6 dosya)
    CodeGen/           # Native kod üretimi
    LSP/               # Language Server Protocol
    REPL/              # İnteraktif REPL
    Driver/            # CLI sürücüsü, proje konfigürasyonu (TOML)
  tests/
    unit/              # GoogleTest birim testleri (2064 test)
    integration/       # Uçtan uca .liva programları
    error/             # Beklenen hata test senaryoları
  examples/            # Örnek Liva programları
  stdlib/              # Standart kütüphane ve runtime
  cmake/               # CMake modülleri
```

## Test Paketi

19 test dosyasında 2064 test:

| Bileşen | Test Sayısı | Kapsam |
|---------|-------------|--------|
| Sema | 645 | Tip kontrolü, ownership, generics, sınıflar, FFI, comptime, macro'lar, UI |
| ProjectConfig | 241 | TOML ayrıştırma, SemVer, bağımlılıklar, lock dosyaları, uzak registry |
| Integration | 196 | Uçtan uca programlar, hata kurtarma, modül sistemi |
| LSP | 153 | JSON, yaşam döngüsü, senkronizasyon, completion, hover, definition, code actions |
| Parser | 149 | Bildirimler, ifadeler, generics, closure'lar, protocol'ler, sınıflar |
| UI Module | 111 | Widget tipleri, layout, tema, animasyon, odak, tooltip |
| Ownership | 98 | Move, borrow, use-after-move, lifetime, class, closure |
| REPL | 57 | Giriş sınıflandırma, komutlar, çok satırlı, ifade sarmalama |
| Lexer | 56 | Token'lar, literal'ler, yorumlar, pozisyonlar, string interpolation |
| Type | 53 | Tip uyumluluğu, dönüşümler, bit genişlikleri |
| SelfHost | 48 | Self-hosting derleme, async runtime (sadece Clang) |
| DAP | 45 | Koşullu breakpoint'ler, ifade değerlendirici, DWARF debug |
| Macro | 34 | Macro tanımlama, genişletme, hijyen, comptime |
| CodeGen | 21 | LLVM IR üretimi, çapraz derleme hedefleri |
| Plugin | 18 | Plugin API, isimlendirme kuralı, kullanılmayan fonksiyon tespiti |
| StdlibModule | 16 | JSON, time, path, testing, crypto sarmalayıcıları |
| Benchmark | 14 | Bench builtin'leri, bench runner, rapor biçimlendirme |
| DiagColor | 12 | Zengin tanılama biçimlendirme, alt çizgi span'ları, renkli çıktı |
| IncrementalBenchmark | 11 | 100+ dosya artımlı derleme benchmarkları |

Tüm test paketini çalıştırın:

```bash
# MinGW build
ctest --test-dir build --output-on-failure

# Clang build (önerilen, codegen + JIT testlerini içerir)
ctest --test-dir build-clang --output-on-failure
```

## IDE Desteği

Liva 5 editör için destek sunar:

| Editör | Özellikler | Konum |
|--------|-----------|-------|
| **VS Code** | Syntax highlighting, LSP istemcisi, DAP istemcisi | `editors/vscode/` |
| **Neovim** | Syntax, ftdetect, indent, ftplugin + LSP/DAP rehberi | `editors/neovim/` |
| **Emacs** | liva-mode.el major mode + eglot/lsp-mode/dap-mode rehberi | `editors/emacs/` |
| **JetBrains** | TextMate grammar + LSP4IJ plugin rehberi | `editors/jetbrains/` |
| **Notepad++** | UDL XML syntax highlighting | `editors/notepadpp/` |

LSP sunucusu: `livac lsp` (stdio JSON-RPC 2.0)
DAP sunucusu: `livac dap` (stdio Debug Adapter Protocol)

## Katkıda Bulunma

Derleme talimatları, kodlama kuralları ve pull request gönderme hakkında bilgi için [CONTRIBUTING.md](CONTRIBUTING.md) dosyasına bakın.

## Lisans

Bu proje eğitim ve araştırma amaçlı olduğu gibi sunulmaktadır.
