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
11. [JSON](#11-json)
12. [Random (Rastgele)](#12-random-rastgele)
13. [Kanal](#13-kanal)
14. [Görev](#14-görev)
15. [Benchmark](#15-benchmark)
16. [Dönüştürme](#16-dönüştürme)

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

## 11. JSON

```liva
import std::json
```

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `jsonParse` | `(string) -> any` | JSON string'ini değere dönüştür |
| `jsonStringify` | `(any) -> string` | Değeri JSON string'ine dönüştür |

### Örnek

```liva
import std::json

func main() {
    let data = jsonParse("{\"name\": \"Alice\", \"age\": 30}")
    println(data)

    let json = jsonStringify([1, 2, 3])
    println(json)  // [1,2,3]
}
```

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

*Bu API referansı, 15 modüle sahip Liva standart kütüphanesinin 0.2.0 sürümünü kapsar. Kütüphane aktif geliştirme altındadır.*
