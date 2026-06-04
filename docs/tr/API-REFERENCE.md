# Liva Standart Kütüphane API Referansı

Tüm standart kütüphane modülleri için eksiksiz referans. "Yerleşik" olarak listelenen fonksiyonlar herhangi bir `import` ifadesi olmadan global olarak kullanılabilir.

---

## İçindekiler

1. [Çekirdek (Yerleşik)](#1-çekirdek-yerleşik)
2. [Matematik](#2-matematik)
3. [String İşlemleri](#3-string-i̇şlemleri)
4. [Dizi İşlemleri](#4-dizi-i̇şlemleri)
5. [Map İşlemleri](#5-map-i̇şlemleri)
6. [I/O (Girdi/Çıktı)](#6-io-girdiçıktı)
7. [OS (İşletim Sistemi)](#7-os-i̇şletim-sistemi)
8. [Time (Zaman)](#8-time-zaman)
9. [Regex (Düzenli İfadeler)](#9-regex-düzenli-i̇fadeler)
10. [Ağ İletişimi](#10-ağ-i̇letişimi)
11. [JSON (json::json)](#11-json-jsonjson)
12. [Random (Rastgele)](#12-random-rastgele)
13. [Kanal](#13-kanal)
14. [Görev](#14-görev)
15. [Benchmark](#15-benchmark)
16. [Dönüştürme](#16-dönüştürme)
17. [Crypto](#17-crypto)
18. [Async](#18-async)
19. [Path](#19-path)
20. [Testing](#20-testing)
21. [UI](#21-ui)
22. [HTTP Client](#22-http-client-httphttp)
23. [Sync Primitives](#23-sync-primitives-syncsync)
24. [Dosya Sistemi](#24-dosya-sistemi-fsfs)
25. [Regex (OOP)](#25-regex-regexregex)
26. [Ağ (OOP)](#26-ag-netnet)
27. [SQLite](#27-sqlite-sqlitesqlite)
28. [PostgreSQL](#28-postgresql-postgrespostgres)
29. [DB Katmanı](#29-db-katmanı-dbdb)
30. [WebSocket](#30-websocket-websocketwebsocket)
31. [JWT](#31-jwt-jwtjwt)
32. [TOML](#32-toml-tomltoml)
33. [Encoding](#33-encoding-encodingencoding)

---

## 1. Çekirdek (Yerleşik)

Bu fonksiyonlar herhangi bir import olmadan kullanılabilir.

### Çıktı

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `print` | `(any...) -> void` | Değerleri satır sonu olmadan yazdır |
| `println` | `(any...) -> void` | Değerleri satır sonu ile yazdır |
| `format` | `(string, any...) -> string` | `{}` yer tutucularıyla string biçimlendir |

### Tip Denetimi

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `len` | `([T]) -> i64` | Dizi veya string uzunluğu |
| `toString` | `(any) -> string` | Herhangi bir değeri string'e dönüştür |
| `assert` | `(bool, string?) -> void` | Koşulu doğrula; başarısızlıkta panik |

### Tip Dönüştürme

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `parseInt` | `(string) -> i32` | String'i i32'ye dönüştür |
| `parseInt64` | `(string) -> i64` | String'i i64'e dönüştür |
| `parseFloat` | `(string) -> f64` | String'i f64'e dönüştür |

---

## 2. Matematik

```liva
import std::math
```

### Temel Matematik

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `abs` | `(numeric) -> numeric` | Mutlak değer (i32, i64, f32, f64 ile çalışır) |
| `min` | `(T, T) -> T` | İki değerin minimumu |
| `max` | `(T, T) -> T` | İki değerin maksimumu |

### Üs ve Kök

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `sqrt` | `(f64) -> f64` | Karekök |
| `pow` | `(f64, f64) -> f64` | Üs alma: `pow(taban, üs)` |

### Yuvarlama

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `floor` | `(f64) -> f64` | En yakın tam sayıya aşağı yuvarla |
| `ceil` | `(f64) -> f64` | En yakın tam sayıya yukarı yuvarla |
| `round` | `(f64) -> f64` | En yakın tam sayıya yuvarla |

### Trigonometri

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `sin` | `(f64) -> f64` | Sinüs (radyan) |
| `cos` | `(f64) -> f64` | Kosinüs (radyan) |
| `tan` | `(f64) -> f64` | Tanjant (radyan) |

### Logaritma

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `log` | `(f64) -> f64` | Doğal logaritma (e tabanı) |
| `log10` | `(f64) -> f64` | 10 tabanında logaritma |

### Örnek

```liva
import std::math

func main() {
    println(sqrt(16.0))       // 4.0
    println(pow(2.0, 10.0))   // 1024.0
    println(abs(-42))          // 42
    println(sin(3.14159 / 2.0)) // ~1.0
    println(floor(3.7))       // 3.0
    println(ceil(3.2))        // 4.0
}
```

---

## 3. String İşlemleri

String metodları herhangi bir import olmadan tüm `string` değerlerinde kullanılabilir.

### Özellikler

| Metod | Döndürür | Açıklama |
|-------|----------|----------|
| `.length` | `i64` | Karakter sayısı |

### Arama

| Metod | İmza | Açıklama |
|-------|------|----------|
| `.contains` | `(string) -> bool` | Alt string var mı kontrol et |
| `.startsWith` | `(string) -> bool` | Önek kontrolü |
| `.endsWith` | `(string) -> bool` | Sonek kontrolü |
| `.indexOf` | `(string) -> i64` | İlk bulunma indeksi (bulunamazsa -1) |

### Dönüştürme

| Metod | İmza | Açıklama |
|-------|------|----------|
| `.toUpper` | `() -> string` | Büyük harfe dönüştür |
| `.toLower` | `() -> string` | Küçük harfe dönüştür |
| `.trim` | `() -> string` | Baştaki/sondaki boşlukları kaldır |
| `.replace` | `(string, string) -> string` | Tüm eşleşmeleri değiştir |
| `.substring` | `(i64, i64) -> string` | Başlangıç ve bitiş indeksine göre alt string çıkar |
| `.split` | `(string) -> [string]` | Ayırıcıya göre böl |

### Örnek

```liva
func main() {
    let s = "Hello, World!"
    println(s.length)              // 13
    println(s.contains("World"))   // true
    println(s.toUpper())           // HELLO, WORLD!
    println(s.replace("World", "Liva"))  // Hello, Liva!
    println(s.split(", "))         // ["Hello", "World!"]
    println(s.substring(0, 5))     // Hello
}
```

---

## 4. Dizi İşlemleri

Dizi metodları herhangi bir import olmadan tüm `[T]` değerlerinde kullanılabilir.

### Özellikler

| Özellik | Tip | Açıklama |
|---------|-----|----------|
| `.length` | `i64` | Eleman sayısı |
| `.isEmpty` | `bool` | Dizi boşsa true |

### Değişiklik

| Metod | İmza | Açıklama |
|-------|------|----------|
| `.push` | `(T) -> void` | Sona eleman ekle |
| `.pop` | `() -> T` | Son elemanı çıkar ve döndür |
| `.reverse` | `() -> void` | Diziyi yerinde ters çevir |
| `.sort` | `() -> void` | Diziyi yerinde sırala |

### Arama

| Metod | İmza | Açıklama |
|-------|------|----------|
| `.contains` | `(T) -> bool` | Eleman var mı kontrol et |
| `.indexOf` | `(T) -> i64` | İlk bulunma indeksi (bulunamazsa -1) |

### Yüksek Dereceli

| Metod | İmza | Açıklama |
|-------|------|----------|
| `.map` | `((T) -> U) -> [U]` | Her elemanı dönüştür |
| `.filter` | `((T) -> bool) -> [T]` | Eşleşen elemanları tut |
| `.reduce` | `(U, (U, T) -> U) -> U` | Tek bir değere indirge |
| `.forEach` | `((T) -> void) -> void` | Her eleman için fonksiyon çalıştır |

### Örnek

```liva
func main() {
    var nums = [5, 3, 1, 4, 2]
    nums.sort()
    println(nums)  // [1, 2, 3, 4, 5]

    let doubled = nums.map(|x: i32| -> i32 { return x * 2 })
    println(doubled)  // [2, 4, 6, 8, 10]

    let sum = nums.reduce(0, |acc: i32, x: i32| -> i32 { return acc + x })
    println(sum)  // 15
}
```

---

## 5. Map İşlemleri

Map metodları herhangi bir import olmadan tüm `Map<K, V>` değerlerinde kullanılabilir.

### Özellikler

| Özellik | Tip | Açıklama |
|---------|-----|----------|
| `.size` | `i64` | Anahtar-değer çifti sayısı |
| `.isEmpty` | `bool` | Map boşsa true |

### Metodlar

| Metod | İmza | Açıklama |
|-------|------|----------|
| `.insert` | `(K, V) -> void` | Anahtar-değer çifti ekle veya güncelle |
| `.get` | `(K) -> V?` | Anahtara göre değer al (opsiyonel döndürür) |
| `.contains` | `(K) -> bool` | Anahtar var mı kontrol et |
| `.remove` | `(K) -> void` | Anahtar-değer çiftini kaldır |
| `.keys` | `() -> [K]` | Tüm anahtarları dizi olarak al |
| `.values` | `() -> [V]` | Tüm değerleri dizi olarak al |

### Örnek

```liva
func main() {
    var scores: Map<string, i32>
    scores.insert("Alice", 95)
    scores.insert("Bob", 87)

    if let score = scores.get("Alice") {
        println("Alice: \(score)")  // Alice: 95
    }

    for (name, score) in scores {
        println("\(name): \(score)")
    }
}
```

---

## 6. I/O (Girdi/Çıktı)

```liva
import std::io
```

### Konsol

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `readLine` | `() -> string` | Standart girdiden bir satır oku |

### Dosya İşlemleri

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `readFile` | `(string) -> string` | Dosyanın tüm içeriğini oku |
| `writeFile` | `(string, string) -> void` | String'i dosyaya yaz (oluşturur/üzerine yazar) |
| `appendFile` | `(string, string) -> void` | String'i dosyanın sonuna ekle |
| `fileExists` | `(string) -> bool` | Dosyanın var olup olmadığını kontrol et |

### Örnek

```liva
import std::io

func main() {
    // Write to file
    writeFile("output.txt", "Hello, World!\n")
    appendFile("output.txt", "Second line\n")

    // Read from file
    if fileExists("output.txt") {
        let content = readFile("output.txt")
        println(content)
    }

    // Console input
    print("Enter your name: ")
    let name = readLine()
    println("Hello, \(name)!")
}
```

---

## 7. OS (İşletim Sistemi)

```liva
import std::os
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `env` | `(string) -> string` | Ortam değişkenini al |
| `exit` | `(i32) -> void` | Belirtilen kodla işlemden çık |
| `args` | `() -> [string]` | Komut satırı argümanlarını al |
| `exec` | `(string) -> i32` | Kabuk komutunu çalıştır, çıkış kodunu döndür |
| `cwd` | `() -> string` | Geçerli çalışma dizinini al |

### Örnek

```liva
import std::os

func main() {
    let home = env("HOME")
    println("Home: \(home)")

    let arguments = args()
    println("Arg count: \(len(arguments))")

    let code = exec("echo hello")
    println("Exit code: \(code)")
}
```

---

## 8. Time (Zaman)

```liva
import std::time
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `now` | `() -> f64` | Epoch'tan bu yana saniye cinsinden geçerli zaman |
| `sleep` | `(i32) -> void` | Belirtilen milisaniye kadar bekle |
| `clock` | `() -> f64` | Yüksek çözünürlüklü saat (saniye) |
| `clockMs` | `() -> i64` | Yüksek çözünürlüklü saat (milisaniye) |

### Takvim Tipleri (time::time)

```liva
import time::time
```

- `Date.parse(s) -> Date` (`"YYYY-MM-DD"` ayrıştırır), `.year()/.month()/.day() -> i32`, `.toString() -> String`.
- `Time.parse(s) -> Time` (`"HH:MM:SS"` ayrıştırır), `.hour()/.minute()/.second() -> i32`, `.toString() -> String`.
- `DateTime` her ikisini birleştirir; tipli DB accessor'ları tarafından dönüş tipi olarak kullanılır.

### Örnek

```liva
import std::time

func main() {
    let start = clockMs()
    sleep(100)
    let elapsed = clockMs() - start
    println("Elapsed: \(elapsed)ms")
}
```

---

## 9. Regex (Düzenli İfadeler)

```liva
import std::regex
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `regexMatch` | `(string, string) -> bool` | String'in kalıpla eşleşip eşleşmediğini test et |
| `regexFind` | `(string, string) -> string` | İlk eşleşmeyi bul |
| `regexFindAll` | `(string, string) -> [string]` | Tüm eşleşmeleri bul |
| `regexReplace` | `(string, string, string) -> string` | Eşleşmeleri belirtilen string ile değiştir |

### Örnek

```liva
import std::regex

func main() {
    let text = "Order #42 has 3 items at $9.99 each"

    println(regexMatch(text, "[0-9]+"))  // true

    let first = regexFind(text, "[0-9]+")
    println(first)  // 42

    let all = regexFindAll(text, "[0-9]+\\.?[0-9]*")
    println(all)  // ["42", "3", "9.99"]

    let cleaned = regexReplace(text, "[0-9]+", "N")
    println(cleaned)  // Order #N has N items at $N.N each
}
```

---

## 10. Ağ İletişimi

```liva
import std::net
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `httpGet` | `(string) -> string` | HTTP GET isteği, gövdeyi döndürür |
| `httpPost` | `(string, string) -> string` | Gövdeli HTTP POST, yanıtı döndürür |
| `httpPut` | `(string, string) -> string` | Gövdeli HTTP PUT, yanıtı döndürür |
| `httpDelete` | `(string) -> string` | HTTP DELETE isteği, yanıtı döndürür |

### Örnek

```liva
import std::net

func main() {
    let body = httpGet("https://httpbin.org/get")
    println(body)

    let response = httpPost("https://httpbin.org/post", "{\"key\": \"value\"}")
    println(response)
}
```

**Platform notları:**
- Windows: WinHTTP kullanır
- Linux/macOS: libcurl kullanır (kurulu olmalıdır)

---

## 11. JSON (json::json)

```liva
import json::json
```

`json::json`, bir ayrıştırma-ağacı (DOM) JSON kütüphanesidir. Belgeyi bir kez ayrıştırın ve bellek içi ağaçta hızlıca gezinin: tipli accessor'lar, `obj["anahtar"]`/`arr[i]` indeksleyiciler, nokta-yolu okumaları ve otomatik oluşturma (auto-vivifying) yolu yazmaları mevcuttur. `Json.object()` veya `Json.array()` çağrısıyla taze nesneler ve diziler oluşturup `set*`/`add*` yardımcılarıyla değer ekleyerek akıcı biçimde JSON yapılandırabilirsiniz.

### Sahiplik sözleşmesi

`Json.parse`, `Json.object` veya `Json.array` tarafından döndürülen değer, belge ağacının **tek sahibidir** ve bir `let` ya da `var`'a bağlanmalıdır. Sahibinden ulaşılan her değer — `obj["k"]`, `arr[i]`, `.getObject()`, `.getArray()`, `.object()`, `.array()`, `.path()` aracılığıyla — yalnızca sahibi yaşadığı sürece geçerli bir **ödünçtür**. Belge, sahibi kapsam dışına çıktığında otomatik olarak serbest bırakılır (`Drop` protokolü). Bir parse geçicisini (`temporary`) zincirlemeyin (örn. `Json.parse(s).object()`) — geçici sahibi hemen serbest bırakılır ve elde edilen ödünç sarkan bir referans olur (bellek sızıntısı/use-after-free).

```liva
// DOĞRU — sahip bir değişkene bağlandı
let doc = Json.parse(benimJsonum)
let root = doc.object()          // ödünç: doc yaşadığı sürece geçerli
println(root.getString("name"))

// YANLIŞ — parse sonucu geçicidir; kullanılmadan önce sahip yok edilir
// let root = Json.parse(benimJsonum).object()   // bunu yapmayın
```

### enum JsonKind

| Durum | Açıklama |
|-------|----------|
| `Null` | JSON `null` |
| `Bool` | JSON boolean |
| `Int` | JSON tam sayı |
| `Double` | JSON ondalık sayı |
| `Str` | JSON string |
| `Arr` | JSON dizi |
| `Obj` | JSON nesne |

### struct Json

Statik fabrika — bir belge oluşturur veya ayrıştırır. Döndürülen değer belgenin sahibidir.

| Metot | İmza | Açıklama |
|-------|------|----------|
| `parse` | `(s: String) -> JsonValue` | JSON string'ini ayrıştırır; kök düğümü sahip olarak döndürür |
| `object` | `() -> JsonObject` | Yeni boş bir JSON nesne belgesi oluşturur |
| `array` | `() -> JsonArray` | Yeni boş bir JSON dizi belgesi oluşturur |

### struct JsonValue

Tek bir JSON düğümü. `owns` `true` ise belgenin sahibidir ve bırakılınca ağacı serbest bırakır; aksi hâlde bir ödünçtür.

**Tür kontrolü**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `kind` | `() -> JsonKind` | Düğümün türünü döndürür |
| `isNull` | `() -> bool` | Düğüm JSON null ise true |
| `isBool` | `() -> bool` | Düğüm boolean ise true |
| `isInt` | `() -> bool` | Düğüm tam sayı ise true |
| `isDouble` | `() -> bool` | Düğüm ondalık ise true |
| `isString` | `() -> bool` | Düğüm string ise true |
| `isArray` | `() -> bool` | Düğüm dizi ise true |
| `isObject` | `() -> bool` | Düğüm nesne ise true |

**Skaler okuma** (yanlış türde varsayılana düşer: `""`, `0`, `0.0`, `false`)

| Metot | İmza | Açıklama |
|-------|------|----------|
| `asString` | `() -> String` | String olarak oku |
| `asInt` | `() -> i64` | Tam sayı olarak oku |
| `asFloat` | `() -> f64` | Ondalık olarak oku (JSON tam sayıları da kabul eder) |
| `asBool` | `() -> bool` | Boolean olarak oku |

**Gezinme (ödünç)**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `object` | `() -> JsonObject` | Bu düğümü nesne ödüncü olarak yeniden yorumla |
| `array` | `() -> JsonArray` | Bu düğümü dizi ödüncü olarak yeniden yorumla |

**Serileştirme**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `toString` | `() -> String` | Sıkıştırılmış JSON string'i |
| `toStringPretty` | `(indent: i32) -> String` | Girintili JSON string'i |

### struct JsonObject

Bir JSON nesne düğümüne ödünç (ya da sahip) tutamaç.

**Abonelik indeksleyici** — `obj["anahtar"]` bir `JsonValue` ödüncü döndürür.

**İnceleme**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `has` | `(key: String) -> bool` | Anahtar mevcutsa true |
| `count` | `() -> i32` | Anahtar sayısı |
| `keys` | `() -> [String]` | Tüm anahtarlar string dizisi olarak |

**Okuma — get\* (eksik/yanlış türde varsayılana düşer)**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `get` | `(key: String) -> JsonValue` | Anahtarın ham düğüm ödüncü |
| `getString` | `(key: String) -> String` | String değer veya `""` |
| `getInt` | `(key: String) -> i64` | Tam sayı değer veya `0` |
| `getFloat` | `(key: String) -> f64` | Ondalık değer veya `0.0` (JSON tam sayıları da kabul eder) |
| `getBool` | `(key: String) -> bool` | Boolean değer veya `false` |
| `getObject` | `(key: String) -> JsonObject` | Anahtar için nesne ödüncü |
| `getArray` | `(key: String) -> JsonArray` | Anahtar için dizi ödüncü |

**Okuma — try\* (eksik/yanlış türde nil döner)**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `tryString` | `(key: String) -> String?` | String veya `nil` |
| `tryInt` | `(key: String) -> i64?` | Tam sayı veya `nil` |
| `tryFloat` | `(key: String) -> f64?` | Ondalık veya `nil` (JSON tam sayıları da kabul eder) |
| `tryBool` | `(key: String) -> bool?` | Boolean veya `nil` |

**Yazma**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `setString` | `(key: String, val: String)` | String değer yaz |
| `setInt` | `(key: String, val: i64)` | Tam sayı değer yaz |
| `setFloat` | `(key: String, val: f64)` | Ondalık değer yaz |
| `setBool` | `(key: String, val: bool)` | Boolean değer yaz |
| `setNull` | `(key: String)` | Null değer yaz |
| `setObject` | `(key: String) -> JsonObject` | Boş nesne oluştur/değiştir; ödünç döndürür |
| `setArray` | `(key: String) -> JsonArray` | Boş dizi oluştur/değiştir; ödünç döndürür |
| `remove` | `(key: String)` | Anahtarı kaldır |

**Yol erişimi**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `path` | `(p: String) -> JsonValue` | Nokta ile ayrılmış yolla oku, örn. `"kullanici.isim"` |
| `setPathString` | `(p: String, val: String)` | Yolla yaz; ara nesneleri otomatik oluşturur |
| `setPathInt` | `(p: String, val: i64)` | Yolla tam sayı yaz |
| `setPathFloat` | `(p: String, val: f64)` | Yolla ondalık yaz |
| `setPathBool` | `(p: String, val: bool)` | Yolla boolean yaz |

**Serileştirme**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `toString` | `() -> String` | Sıkıştırılmış JSON string'i |
| `toStringPretty` | `(indent: i32) -> String` | Girintili JSON string'i |

### struct JsonArray

Bir JSON dizi düğümüne ödünç (ya da sahip) tutamaç.

**Abonelik indeksleyici** — `arr[i]` (`i: i64`) bir `JsonValue` ödüncü döndürür.

**İnceleme**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `count` | `() -> i32` | Eleman sayısı |
| `length` | `() -> i32` | `count` için takma ad |

**Okuma**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `at` | `(i: i64) -> JsonValue` | İndeksteki ham düğüm ödüncü |
| `getString` | `(i: i64) -> String` | İndeksteki string veya `""` |
| `getInt` | `(i: i64) -> i64` | İndeksteki tam sayı veya `0` |
| `getFloat` | `(i: i64) -> f64` | İndeksteki ondalık veya `0.0` |
| `getBool` | `(i: i64) -> bool` | İndeksteki boolean veya `false` |
| `getObject` | `(i: i64) -> JsonObject` | İndeksteki nesne ödüncü |
| `getArray` | `(i: i64) -> JsonArray` | İndeksteki dizi ödüncü |

**Yazma**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `addString` | `(val: String)` | String eleman ekle |
| `addInt` | `(val: i64)` | Tam sayı eleman ekle |
| `addFloat` | `(val: f64)` | Ondalık eleman ekle |
| `addBool` | `(val: bool)` | Boolean eleman ekle |
| `addNull` | `()` | Null eleman ekle |
| `addObject` | `() -> JsonObject` | Boş nesne ekle; ödünç döndürür |
| `addArray` | `() -> JsonArray` | Boş dizi ekle; ödünç döndürür |

**Serileştirme**

| Metot | İmza | Açıklama |
|-------|------|----------|
| `toString` | `() -> String` | Sıkıştırılmış JSON string'i |
| `toStringPretty` | `(indent: i32) -> String` | Girintili JSON string'i |

### Varsayılana düşme ile try\* semantiği

`JsonObject` üzerindeki `getString`/`getInt`/`getFloat`/`getBool` ve `JsonArray` üzerindeki eşdeğer indeksli okumalar her zaman bir değer döndürür — anahtar yoksa ya da düğüm yanlış türdeyse `""`, `0`, `0.0` veya `false`. "Anahtar yok" ile "varsayılan değerli anahtar mevcut" arasında ayrım yapmanız gerektiğinde `try*` varyantlarını (`tryString`, `tryInt`, `tryFloat`, `tryBool`) kullanın. `asFloat`/`tryFloat` ayrıca JSON tam sayı düğümlerini de kabul eder (otomatik dönüşüm yapar).

### Örnek

```liva
import json::json

func main() {
    // Ayrıştırma ve okuma
    let doc = Json.parse("{\"user\":{\"name\":\"liva\",\"age\":3},\"tags\":[\"a\",\"b\"]}")
    let root = doc.object()
    println(root.path("user.name").asString())        // liva
    println(root.getObject("user").getInt("age"))      // 3
    println(root.getArray("tags").getString(0 as i64)) // a
    println(root["user"]["name"].asString())           // liva (indeksleyici)

    // Oluşturma ve serileştirme
    var out = Json.object()
    out.setString("name", "yeni")
    var tags = out.setArray("tags")
    tags.addString("x")
    out.setPathInt("meta.count", 1)
    println(out.toString())
}
```

### v1 sınırlamaları

- Yalnızca katı JSON: yorumlar ve sondaki virgüller ayrıştırma hatasıdır.
- `setPath*` ara düğümleri otomatik olarak **nesne** olarak oluşturur; sayısal yol segmentleri (örn. `"items.0.name"`) dizi indeksi değil, nesne anahtarı olarak işlenir.
- String literal'lerdeki `\uXXXX` vekil çiftleri tek bir kod noktasına birleştirilmez.
- Abonelik atama (`obj["k"] = v`) desteklenmez; `setString`, `setInt` vb. kullanın.

---

## 12. Random (Rastgele)

```liva
import std::random
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `random` | `() -> f64` | [0.0, 1.0) aralığında rastgele ondalık sayı |
| `randInt` | `(i32, i32) -> i32` | [min, max] aralığında rastgele tam sayı |
| `randFloat` | `() -> f64` | `random()` için takma ad |
| `randomChoice` | `([T]) -> T` | Diziden rastgele eleman seç |

### Örnek

```liva
import std::random

func main() {
    let dice = randInt(1, 6)
    println("Dice: \(dice)")

    let coin = randomChoice(["heads", "tails"])
    println("Coin: \(coin)")

    let probability = random()
    println("Random: \(probability)")
}
```

---

## 13. Kanal

```liva
import std::channel
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `channelCreate` | `(i32) -> Channel` | Belirtilen kapasitede tamponlu kanal oluştur |
| `channelSend` | `(Channel, any) -> void` | Kanala bir değer gönder |
| `channelRecv` | `(Channel) -> any` | Kanaldan bir değer al (boşsa bekler) |
| `channelClose` | `(Channel) -> void` | Kanalı kapat (artık gönderim yapılamaz) |

### Örnek

```liva
import std::channel

func main() {
    let ch = channelCreate(10)

    channelSend(ch, "hello")
    channelSend(ch, "world")

    println(channelRecv(ch))  // hello
    println(channelRecv(ch))  // world

    channelClose(ch)
}
```

### Özellikler

- **Tamponlu**: Kanallar sabit bir kapasiteye sahiptir; dolduğunda gönderimler bloklanır
- **Dairesel tampon**: Dahili uygulama dairesel tampon kullanır
- **İş parçacığı güvenli**: Döngüsel bekleme (spin-wait) senkronizasyonu kullanır

---

## 14. Görev

```liva
import std::task
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `taskGroupCreate` | `() -> TaskGroup` | Yeni bir görev grubu oluştur |
| `taskGroupSpawn` | `(TaskGroup, () -> void) -> void` | Grupta bir görev başlat |
| `taskGroupAwaitAll` | `(TaskGroup) -> void` | Tüm görevlerin tamamlanmasını bekle |
| `taskGroupCancelAll` | `(TaskGroup) -> void` | Tüm çalışan görevleri iptal et |

### Örnek

```liva
import std::task

func main() {
    let group = taskGroupCreate()

    taskGroupSpawn(group, || {
        println("Task A")
    })

    taskGroupSpawn(group, || {
        println("Task B")
    })

    taskGroupAwaitAll(group)
    println("All tasks done")
}
```

---

## 15. Benchmark

```liva
import std::bench
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `benchStart` | `(string) -> void` | İsimli bir benchmark zamanlayıcısını başlat |
| `benchIter` | `(string) -> void` | Bir benchmark iterasyonunu kaydet |
| `benchDone` | `(string) -> void` | İsimli bir benchmark'ı sonlandır |
| `benchReport` | `() -> void` | Tüm benchmark'ların sonuçlarını yazdır |
| `benchReset` | `() -> void` | Tüm benchmark verilerini sıfırla |

### Örnek

```liva
import std::bench

func main() {
    benchStart("sort")
    for i in 0..100 {
        benchIter("sort")
        var arr = [5, 3, 1, 4, 2]
        arr.sort()
    }
    benchDone("sort")

    benchReport()
    // Output: sort: 100 iterations, avg 0.005ms, total 0.5ms
}
```

---

## 16. Dönüştürme

```liva
import std::convert
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `toString` | `(any) -> string` | Değeri string temsiline dönüştür |
| `parseInt` | `(string) -> i32` | String'i 32-bit tam sayıya dönüştür |
| `parseInt64` | `(string) -> i64` | String'i 64-bit tam sayıya dönüştür |
| `parseFloat` | `(string) -> f64` | String'i 64-bit ondalık sayıya dönüştür |
| `toBool` | `(string) -> bool` | Esnek bool ayrıştırma: `1/t/true/yes/on` (her durum) → `true`; diğerleri → `false` (convert::convert) |

### Örnek

```liva
import std::convert

func main() {
    let s = toString(42)
    println(s)              // "42"

    let n = parseInt("123")
    println(n + 1)          // 124

    let f = parseFloat("3.14")
    println(f * 2.0)        // 6.28
}
```

---

## 17. Crypto

```liva
import std::crypto
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `sha256` | `(string) -> string` | Girdi stringinin SHA-256 hash'i |
| `md5` | `(string) -> string` | Girdi stringinin MD5 hash'i |
| `hmacSha256` | `(string, string) -> string` | Anahtar ve mesajla HMAC-SHA256 |

### Örnek

```liva
import std::crypto

func main() {
    let hash = sha256("hello world")
    println(hash)

    let mac = hmacSha256("gizli-anahtar", "mesaj")
    println(mac)
}
```

---

## 18. Async

```liva
import std::async
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `taskSelect` | `([Task]) -> Task` | İlk tamamlanan görevi bekle |
| `withTimeout` | `(Task, i64) -> Task` | Zaman aşımıyla görev çalıştır (ms) |
| `schedulerInit` | `(i32) -> void` | N işçili iş parçacığı havuzunu başlat |
| `schedulerShutdown` | `() -> void` | İş parçacığı havuzunu kapat |
| `asyncFileRead` | `(string) -> string` | Asenkron dosya okuma |
| `asyncFileWrite` | `(string, string) -> void` | Asenkron dosya yazma |

---

## 19. Path

```liva
import std::path
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `pathJoin` | `(string, string) -> string` | İki yol bileşenini birleştir |
| `pathExtension` | `(string) -> string` | Dosya uzantısını al |
| `pathBasename` | `(string) -> string` | Yoldan dosya adını al |
| `pathDirname` | `(string) -> string` | Yoldan dizin adını al |
| `pathIsAbsolute` | `(string) -> bool` | Yolun mutlak olup olmadığını kontrol et |

---

## 20. Testing

```liva
import std::testing
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `assertEqual` | `(any, any) -> void` | İki değerin eşit olduğunu doğrula |
| `assertNotEqual` | `(any, any) -> void` | İki değerin farklı olduğunu doğrula |
| `assertTrue` | `(bool) -> void` | Koşulun doğru olduğunu doğrula |
| `assertFalse` | `(bool) -> void` | Koşulun yanlış olduğunu doğrula |

---

## 21. UI

```liva
import std::ui
```

UI modülü, 12 geliştirme fazına sahip raylib tabanlı bir widget sistemi sunar:

- **Canvas**: Pencere yönetimi, çizim ilkelleri (dikdörtgen, daire, çizgi, metin)
- **Widget'lar**: Button, Label, TextInput, Checkbox, Slider, ScrollView, ProgressBar, RadioGroup, TabView, Dropdown, Dialog, TextArea, Tooltip, Popover
- **Layout**: VStack, HStack, Grid, Hizalı/Boşluklu/Dolgulu konteynerler
- **Tema**: Karanlık/aydınlık ön ayarlı Theme yapısı, temalı widget fabrikaları
- **Animasyon**: Easing fonksiyonları, Tween, ColorTransition, HoverAnimator
- **Odak**: FocusManager (Tab/Ok tuşu navigasyonu), FocusRing, KeyAction kısayolları
- **Olaylar**: Callback alanları (onClick, onToggle, onValueChange)

### Örnek

```liva
import std::ui

func main() {
    initWindow(800, 600, "Uygulamam")
    let theme = Theme.dark()

    while !windowShouldClose() {
        beginDrawing()
        clearBackground(theme.background)

        let btn = Button.themed(theme, 100.0, 50.0, 200.0, 40.0, "Tıkla")
        btn.draw()

        endDrawing()
    }
    closeWindow()
}
```

---

## 22. HTTP Client (http::http)

```liva
import http::http

let client = HttpClient.new()
let resp = client.get("/api/users")

let client2 = HttpClient.withBaseUrl("https://api.example.com")
let r = client2.post("/data", "{}")
let r2 = client2.put("/data/1", "{}")
let r3 = client2.delete("/data/1")
```

**HttpClient**: new(), withBaseUrl(url), get(url), post(url, body), put(url, body), patch(url, body), delete(url)

## 23. Sync Primitives (sync::sync)

```liva
import sync::sync

var m = Mutex.new()
m.lock()
m.unlock()
m.free()

var a = AtomicI64.new(0)
a.store(42)
let v = a.load()
a.add(1)
a.free()

var ch = Channel.new(10)
ch.send(42)
let val = ch.receive()
ch.free()
```

**Mutex**: new(), lock(), unlock(), tryLock(), free()
**AtomicI64**: new(value), load(), store(value), add(delta), sub(delta), compareAndSwap(expected, desired), free()
**Channel**: new(capacity), send(value), receive(), close(), len(), free()
**TaskGroup**: new(), awaitAll(), cancelAll(), count(), free()

## 24. Dosya Sistemi (fs::fs)

```liva
import fs::fs

let info = FileInfo.new("/path/to/file")
let name = info.name()
let ext = info.extension()
let exists = info.exists()

let dir = Dir.new("/path")
let files = dir.list()
```

**FileInfo**: new(path), name(), extension(), parent(), exists(), toString()
**Dir**: new(path), exists(), list(), toString()

## 25. Regex (regex::regex)

```liva
import regex::regex

let re = Regex.new("[0-9]+")
let matched = re.isMatch("hello 123")
let found = re.find("abc 42 def")
let all = re.findAll("1 ve 2 ve 3")
let replaced = re.replace("abc123", "NUM")
```

**Regex**: new(pattern), isMatch(text), find(text), findAll(text), replace(text, replacement), groups(text), toString()

## 26. Ag (net::net)

```liva
import net::net

let url = Url.parse("https://example.com")
let req = Request.get("https://api.example.com")
let req2 = Request.post("https://api.example.com", "data")
```

**Url**: parse(s), toString()
**Request**: get(url), post(url, body), put(url, body), delete(url)

---

## 27. SQLite (sqlite::sqlite)

Gömülü SQL veritabanı. Windows 10+ üzerinde runtime `winsqlite3.dll`'i
dinamik olarak yükler; sistem SQLite'ı bulunmayan platformlarda her
giriş noktası sessizce başarısız olur (`open` → `nil`, `exec` →
`false`, sorgular → `nil`/boş).

```liva
import sqlite::sqlite

if let d = SqliteDB.openMemory() {
    var db = d
    db.exec("CREATE TABLE u (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")

    // Parametre bağlamalı prepared statement (1 tabanlı indeks).
    if let p = db.prepare("INSERT INTO u (name, age) VALUES (?, ?)") {
        var ins = p
        ins.bindText(1, "Alice")
        ins.bindInt(2, 30 as i64)
        ins.step()
        ins.finalize()
    }

    // Satırları döngüyle gez (kolon indeksleri 0 tabanlı).
    if let p = db.prepare("SELECT name, age FROM u WHERE age > ?") {
        var q = p
        q.bindInt(1, 18 as i64)
        while q.step() {
            println(q.columnText(0))
            println(q.columnInt(1))
        }
        q.finalize()
    }
    db.close()
}
```

### Yapılar

| Yapı | Metotlar | Açıklama |
|------|----------|----------|
| `SqliteDB` | `open`, `openMemory`, `exec`, `queryString`, `queryInt`, `queryColumn`, `prepare`, `lastInsertId`, `changes`, `errorMessage`, `close` | Tek veritabanı bağlantısı (`":memory:"` veya dosya yolu) |
| `Stmt` | `bindText`, `bindInt`, `bindDouble`, `bindNull`, `step`, `reset`, `columnCount`, `columnText`, `columnInt`, `columnDouble`, `finalize` | Parametre bağlamalı, satır iterasyonlu derlenmiş SQL ifadesi |

`step()` satır varsa `true`, ifade tamamlandığında ya da hata
oluştuğunda `false` döner. Bind indeksleri **1 tabanlı**, kolon
indeksleri **0 tabanlı** (SQLite C API ile aynı). `bindText` içeride
`SQLITE_TRANSIENT` kullanır; SQLite girdiyi kopyalar — Liva string'i
hareket etse veya serbest bırakılsa da prepared statement bozulmaz.
Bu sayede `'; DROP TABLE; --` içeren değerler bile güvenle
saklanabilir.

### Yeni SQLite metotları

- `Stmt.columnName(col) -> String` — sonuç kolon adı.
- `Stmt.columnType(col) -> i32` — 1=INTEGER, 2=FLOAT, 3=TEXT, 4=BLOB, 5=NULL.
- `Stmt.columnIsNull(col) -> bool` — hücre NULL ise true.
- `Stmt.bindByName(name, val) -> bool` — `:name`/`@name`/`$name` parametresine text bağlar.
- `Stmt.bindBlob(idx, [u8]) -> bool`, `Stmt.columnBlob(col) -> [u8]` — ikili veri.
- `SqliteDB.begin()/commit()/rollback() -> bool` — transaction kontrolü.
- `Stmt.columnBool(col) -> bool`, `columnDate(col) -> Date`, `columnTime(col) -> Time`, `columnDateTime(col) -> DateTime` — hücre metninden ayrıştırılan tipli kolon okuma (`columnDouble` zaten vardı).

Tipli accessor'lar non-optional; ayrıştırılamayan metinde varsayılan (`0.0`/`false`/epoch) döner. `columnBool` `1/t/true/yes/on` değerlerini (her durum) kabul eder.

---

## 28. PostgreSQL (postgres::postgres)

libpq tabanlı istemci, çalışma anında dinamik çözülür. libpq veya sunucu yoksa
`PgConn.open` `nil`, `exec` `false`, sorgular `nil` döndürür (fail-closed).
Windows'ta yükleyici `C:\Program Files\PostgreSQL\<sürüm>\bin\libpq.dll`'i
(en yeni önce) ve `$LIVA_LIBPQ_PATH`'i yoklar.

- `PgConn.open(connString) -> PgConn?` — `"host=... dbname=... user=..."`.
- `PgConn.exec(sql) -> bool` — satır döndürmeyen komut.
- `PgConn.query(sql) -> PgResult?` / `queryParams(sql, [String]) -> PgResult?`.
- `PgConn.errorMessage() -> String`, `close()`.
- `PgResult.rowCount()/colCount() -> i32`, `getText(r,c)/getInt(r,c)`,
  `isNull(r,c) -> bool`, `columnName(c) -> String`, `clear()`.
  Ayrıca tipli okumalar: `getDouble(r,c) -> f64`, `getBool(r,c) -> bool`, `getDate(r,c) -> Date`, `getTime(r,c) -> Time`, `getDateTime(r,c) -> DateTime`.

---

## 29. DB Katmanı (db::db)

Sürücü-bağımsız katman. Her yerde `?` yer tutucusu yazın; PostgreSQL adapteri
bunları `$1,$2,...`'e çevirir. Çevirici şunların içindeki `?`'leri atlar: tek
tırnaklı string literal'ler (`'...'`, `''` escape), satır yorumları (`-- ...`),
blok yorumları (`/* ... */`) ve dollar-quote'lu string'ler (`$$...$$`,
`$tag$...$tag$`); rakamla başlayan `$1` olduğu gibi bırakılır.

- `protocol Database { exec; query(sql, [String]) -> [Row]; lastInsertId;
  errorMessage; close }`
- `SqliteDatabase.open(path)? / openMemory()?` — `impl Database`.
- `PgDatabase.open(connString)?` — `impl Database` (`lastInsertId` 0 döndürür;
  `RETURNING` kullanın).
- `Row.getText(col)/getInt(col)/isNull(col)/byName(name) -> String?`.
  Ayrıca tipli okumalar: `getDouble(col) -> f64`, `getBool(col) -> bool`, `getDate(col) -> Date`, `getTime(col) -> Time`, `getDateTime(col) -> DateTime`.
- İki sürücüde de çalışan kod için `dyn Database` kullanın.

Tipli accessor'lar non-optional; ayrıştırılamayan metinde varsayılan (`0.0`/`false`/epoch) döner. `getBool` `1/t/true/yes/on` değerlerini (her durum) kabul eder.

---

## 30. WebSocket (websocket::websocket)

Windows üzerinde `WinHTTP` ile çalışan WebSocket istemcisi; URL'ler
`ws://` veya `wss://` şemasıyla başlar. WinHTTP bulunmayan
platformlarda her çağrı `nil`/`false` döner.

```liva
import websocket::websocket

if let s = WebSocket.connect("wss://echo.example.com/socket") {
    var ws = s
    ws.send("merhaba")
    if let yanit = ws.recv() {
        println(yanit)
    }
    ws.close()
}
```

### Yapılar

| Yapı | Metotlar | Açıklama |
|------|----------|----------|
| `WebSocket` | `connect`, `send`, `recv`, `isOpen`, `close`, `closeWith` | UTF-8 metin çerçeveleri taşıyan tek WebSocket bağlantısı |

`recv()` parçalı mesajları otomatik olarak birleştirir; karşı taraf
bağlantıyı kapatınca `nil` döner. `close()` standart 1000 kapatma
kodunu gönderir; `closeWith(kod, sebep)` özel kod ve sebep
göndermenize izin verir.

---

## 31. JWT (jwt::jwt)

HMAC-SHA256 (HS256) ve HMAC-SHA512 (HS512) ile imzalanan JSON Web
Token'lar. Doğrulama, runtime `constTimeEq` kullanarak sabit zamanda
HMAC karşılaştırması yapar.

```liva
import jwt::jwt

let token = Jwt.signHS256("gizli-anahtar", "{\"user\":\"alice\",\"exp\":1700000000}")
println(token.toString())

if let payload = Jwt.verifyHS256(token.toString(), "gizli-anahtar") {
    println(payload)
} else {
    println("imza geçersiz")
}
```

### Yapılar

| Yapı | Metotlar | Açıklama |
|------|----------|----------|
| `Jwt` | `signHS256`, `signHS512`, `toString`, `verifyHS256`, `verifyHS512` | header.payload.signature parçalı imzalı JWT token |

`Jwt` sarıcısı kullanmadan da çalışmak isteyenler için
`jwtBuildHS256`, `jwtBuildHS512`, `jwtVerifyHS256`, `jwtVerifyHS512`
serbest fonksiyonları da export edilir. Doğrulama başarıda payload
JSON'unu, başarısız imza/yapıda `nil` döner.

---

## 32. TOML (toml::toml)

TOML 1.0 ayrıştırıcı, isteğe bağlı erişim metotlarıyla. Sorgular
bölüm + anahtar üzerinden yapılır.

```liva
import toml::toml

let doc = TomlDocument.parse("[server]\nhost = \"localhost\"\nport = 8080")
if doc.isValid() {
    if let host = doc.getString("server", "host") {
        println(host)
    }
    if let port = doc.getInt("server", "port") {
        println(port)
    }
    if doc.hasKey("server", "tls") {
        if let tls = doc.getBool("server", "tls") {
            println(tls)
        }
    }
}
```

### Yapılar

| Yapı | Metotlar | Açıklama |
|------|----------|----------|
| `TomlDocument` | `parse`, `isValid`, `getString`, `getInt`, `getBool`, `hasKey`, `free` | Ayrıştırılmış TOML belgesi |

Bir bölümden değer okumak için `getString` / `getInt` / `getBool`
kullanın. Eksik bölüm/anahtar `nil` döner. Paket yöneticisi
`liva.toml` için bu modülü kullanır.

---

## 33. Encoding (encoding::encoding)

Metin kodlamaları (Base64, Base64URL, Hex, URL percent-encoding) ve
RFC 1952 gzip sıkıştırma. Encoder LZ77 + sabit Huffman bloğu ile
çalışır.

```liva
import encoding::encoding

let b64 = toBase64("merhaba")
let geri = fromBase64(b64)

let url = toUrl("merhaba dünya & arkadaşlar")
let cz = fromUrl(url)

let hex = toHex("Hi")
let bin = fromHex(hex)

let urlGuvenli = toBase64Url("subjects?")

// Runtime built-in'leri ile gzip / gunzip
let bytes: [u8] = strToBytes("hello hello hello hello hello")
let gz: [u8] = gzipEncode(bytes)
if let plain = gzipDecode(gz) {
    println(bytesToStr(plain))
}
```

### Yapılar

| Yapı | Metotlar | Açıklama |
|------|----------|----------|
| `Base64` | `encode`, `fromEncoded`, `decode`, `toString` | Base64 (RFC 4648) sarıcı |
| `Base64Url` | `encode`, `fromEncoded`, `decode`, `toString` | URL güvenli Base64 (padding'siz) |
| `Hex` | `encode`, `fromEncoded`, `decode`, `toString` | Küçük harf hex |
| `Url` | `encode`, `fromEncoded`, `decode`, `toString` | URL percent-encoding |

### Serbest fonksiyonlar

| Fonksiyon | Dönüş | Açıklama |
|-----------|-------|----------|
| `toBase64`, `fromBase64` | `String` / `String?` | Base64 turlama |
| `toBase64Url`, `fromBase64Url` | `String` / `String?` | Base64URL (padding'siz) — JWT tarafından kullanılır |
| `toHex`, `fromHex` | `String` / `String?` | Hex turlama |
| `toUrl`, `fromUrl` | `String` / `String?` | Percent-encoding turlama |
| `checksum` | `i64` | CRC32 |
| `gzipEncode(data: [u8]) -> [u8]` | `[u8]` | LZ77 + sabit Huffman ile RFC 1952 gzip |
| `gzipDecode(data: [u8]) -> [u8]?` | `[u8]?` | gunzip; stored, sabit ve dinamik Huffman bloklarını destekler; bozuk girdide nil |

Gömülü NUL byte'ı içeren ikili veri için `[u8]` (byte dizisi)
varyantlarını kullanın: `hexEncodeBytes` / `hexDecodeBytes` /
`base64UrlEncodeBytes` / `base64UrlDecodeBytes` / `strToBytes` /
`bytesToStr`. String tabanlı encoder'lar ilk NUL'da kesilir.

---

*Bu API referansı, 31 modüle sahip Liva standart kütüphanesinin 0.3.0 sürümünü kapsar.*
