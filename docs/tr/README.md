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
- **Araçlar** — LSP sunucusu, interaktif REPL, proje manifest'i (`liva.toml`)

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

# Tanılama araçları
livac --dump-tokens file.liva    # Token akışını göster
livac --dump-ast file.liva       # AST'yi göster
livac --check-only file.liva     # Codegen olmadan tip kontrolü
livac --emit-ir file.liva        # LLVM IR çıktısı
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
    let response = await httpGet(url)
    return response
}
```

### Standart Kütüphane Modülleri

```liva
import std::math      // abs, sqrt, pow, sin, cos, ...
import std::io        // readLine, readFile, writeFile
import std::convert   // parseInt, parseFloat, toString
import std::os        // env, args, exit, exec
import std::random    // randInt, randFloat
import std::regex     // Regex, match, replace
import std::net       // httpGet, httpPost
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
    unit/              # GoogleTest birim testleri (613 test)
    integration/       # Uçtan uca .liva programları
    error/             # Beklenen hata test senaryoları
  examples/            # Örnek Liva programları
  stdlib/              # Standart kütüphane ve runtime
  cmake/               # CMake modülleri
```

## Test Paketi

8 test dosyasında 613 test:

| Bileşen | Test Sayısı | Kapsam |
|---------|-------------|--------|
| Lexer | 41 | Token'lar, literal'ler, yorumlar, pozisyonlar, string interpolation |
| Parser | 82 | Bildirimler, ifadeler, generics, closure'lar, protocol'ler |
| Sema | 321 | Tüm dil özellikleri için kapsamlı semantik analiz |
| Type | 12 | Tip uyumluluğu, dönüşümler, bit genişlikleri |
| Ownership | 9 | Move, borrow, use-after-move, lifetime |
| ProjectConfig | 74 | TOML ayrıştırma, SemVer, bağımlılıklar, lock dosyaları |
| LSP | 37 | JSON, yaşam döngüsü, senkronizasyon, completion, hover, definition |
| REPL | 37 | Giriş sınıflandırma, komutlar, çok satırlı, ifade sarmalama |

Tüm test paketini çalıştırın:

```bash
ctest --test-dir build --output-on-failure
```

## Katkıda Bulunma

Derleme talimatları, kodlama kuralları ve pull request gönderme hakkında bilgi için [CONTRIBUTING.md](CONTRIBUTING.md) dosyasına bakın.

## Lisans

Bu proje eğitim ve araştırma amaçlı olduğu gibi sunulmaktadır.
