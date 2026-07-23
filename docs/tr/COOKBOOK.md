# Liva Yemek Kitabı

Liva programlama dili için yaygın kalıplar, tarifler ve deyimler. Her tarif bağımsızdır ve pratik bir tekniği gösterir.

---

## İçindekiler

1. [`?` Operatörü ile Hata Yönetimi](#1--operatörü-ile-hata-yönetimi)
2. [Builder Kalıbı](#2-builder-kalıbı)
3. [Protocol'lerle Gözlemci Kalıbı](#3-protocollerle-gözlemci-kalıbı)
4. [Özel Iterator](#4-özel-iterator)
5. [Drop Trait ile RAII](#5-drop-trait-ile-raii)
6. [Generik Konteyner](#6-generik-konteyner)
7. [Asenkron HTTP İstekleri](#7-asenkron-http-i̇stekleri)
8. [Kanal Tabanlı Üretici/Tüketici](#8-kanal-tabanlı-üreticitüketici)
9. [C Kütüphaneleri ile FFI](#9-c-kütüphaneleri-ile-ffi)
10. [WASM'a Çapraz Derleme](#10-wasma-çapraz-derleme)
11. [Sınıflar ve Kalıtım](#11-sınıflar-ve-kalıtım)
12. [For Await ve Channel'larla Async](#12-for-await-ve-channellarla-async)
13. [Crypto: Hash ve HMAC](#13-crypto-hash-ve-hmac)
14. [Ayrı Derleme](#14-ayrı-derleme)

---

## 1. `?` Operatörü ile Hata Yönetimi

`?` operatörü bir `Result` değerini açar veya hatayı çağırana yayar.

```liva
enum ParseError {
    case InvalidFormat
    case OutOfRange
}

func parseAge(input: string) -> Result<i32, ParseError> {
    let num = parseInt(input)
    guard num > 0 else {
        return Err(ParseError.OutOfRange)
    }
    guard num < 150 else {
        return Err(ParseError.OutOfRange)
    }
    return Ok(num)
}

func parseUser(name: string, ageStr: string) -> Result<string, ParseError> {
    let age = parseAge(ageStr)?   // propagates error if Err
    return Ok(format("{} is {} years old", name, age))
}

func main() {
    match parseUser("Alice", "30") {
        Ok(msg) => println(msg)
        Err(ParseError.InvalidFormat) => println("Bad format")
        Err(ParseError.OutOfRange) => println("Age out of range")
    }
}
```

**Önemli noktalar:**
- Bir `Result<T, E>` ifadesinin ardından gelen `?` operatörü `Ok(T)` değerini açar veya `Err(E)` ile erken dönüş yapar
- Çağıran fonksiyon da uyumlu bir hata tipiyle `Result` döndürmelidir
- Doğrulama zincirleri için `guard` ile birleştirin

---

## 2. Builder Kalıbı

Karmaşık nesneleri adım adım oluşturmak için metod zincirleme kullanın.

```liva
struct QueryBuilder {
    var table: string
    var conditions: [string]
    var orderBy: string
    var limitVal: i32
}

impl QueryBuilder {
    func new(table: string) -> QueryBuilder {
        return QueryBuilder {
            table: table,
            conditions: [],
            orderBy: "",
            limitVal: 0
        }
    }

    func where_clause(ref mut self, condition: string) -> ref mut QueryBuilder {
        self.conditions.push(condition)
        return self
    }

    func order(ref mut self, field: string) -> ref mut QueryBuilder {
        self.orderBy = field
        return self
    }

    func limit(ref mut self, n: i32) -> ref mut QueryBuilder {
        self.limitVal = n
        return self
    }

    func build(ref self) -> string {
        var sql = "SELECT * FROM " + self.table
        if self.conditions.length > 0 {
            sql = sql + " WHERE "
            for i in 0..self.conditions.length {
                if i > 0 { sql = sql + " AND " }
                sql = sql + self.conditions[i]
            }
        }
        if self.orderBy != "" {
            sql = sql + " ORDER BY " + self.orderBy
        }
        if self.limitVal > 0 {
            sql = sql + " LIMIT " + toString(self.limitVal)
        }
        return sql
    }
}

func main() {
    var query = QueryBuilder.new("users")
    let sql = query
        .where_clause("age > 18")
        .where_clause("active = true")
        .order("name")
        .limit(10)
        .build()
    println(sql)
    // SELECT * FROM users WHERE age > 18 AND active = true ORDER BY name LIMIT 10
}
```

---

## 3. Protocol'lerle Gözlemci Kalıbı

Bildirim arayüzü tanımlamak için protocol'leri kullanın.

```liva
protocol Observer {
    func onEvent(ref self, event: string)
}

struct Logger {}

impl Logger: Observer {
    func onEvent(ref self, event: string) {
        println("[LOG] \(event)")
    }
}

struct Counter {
    var count: i32
}

impl Counter: Observer {
    func onEvent(ref self, event: string) {
        println("[COUNT] Event #\(self.count)")
    }
}

struct EventBus {
    var listeners: [dyn Observer]
}

impl EventBus {
    func new() -> EventBus {
        return EventBus { listeners: [] }
    }

    func subscribe(ref mut self, listener: dyn Observer) {
        self.listeners.push(listener)
    }

    func emit(ref self, event: string) {
        for listener in self.listeners {
            listener.onEvent(event)
        }
    }
}

func main() {
    var bus = EventBus.new()
    bus.subscribe(Logger {})
    bus.subscribe(Counter { count: 1 })
    bus.emit("user_login")
}
```

---

## 4. Özel Iterator

`Iterator` protocol'üne uygun hale getirerek bir tipi `for-in` ile kullanılabilir kılın.

```liva
struct Counter {
    var n: i32
}

impl Counter: Iterator {
    type Item = i32

    func next(mut self) -> i32? {
        if self.n <= 0 {
            return nil
        }
        self.n = self.n - 1
        return self.n + 1
    }
}

func main() {
    var c = Counter { n: 3 }
    for x in c {
        println(x)
    }
    // Çıktı: 3, 2, 1
}
```

Asenkron iterasyon için `AsyncIterator` uygulayın:

```liva
struct AsyncCounter {
    var n: i32
}

impl AsyncCounter: AsyncIterator {
    type Item = i32

    async func next(mut self) -> i32? {
        if self.n <= 0 {
            return nil
        }
        self.n = self.n - 1
        return self.n + 1
    }
}

async func main() {
    var c = AsyncCounter { n: 3 }
    for await x in c {
        println(x)
    }
    // Çıktı: 3, 2, 1
}
```

---

## 5. Drop Trait ile RAII

Otomatik kaynak temizliği için `Drop` protocol'ünü kullanın.

```liva
struct TempFile {
    var path: string
}

impl TempFile {
    func new(path: string, content: string) -> TempFile {
        writeFile(path, content)
        println("Created: \(path)")
        return TempFile { path: path }
    }

    func read(ref self) -> string {
        return readFile(self.path)
    }
}

impl TempFile: Drop {
    func drop(mut self) {
        println("Deleting: \(self.path)")
        // In real code: delete the file
    }
}

func main() {
    let tmp = TempFile.new("/tmp/data.txt", "hello")
    let content = tmp.read()
    println(content)
}   // tmp.drop() called automatically — file cleaned up
```

**Önemli noktalar:**
- `Drop.drop(mut self)` kapsam çıkışında otomatik olarak çağrılır
- Drop, tanımlama sırasının tersinde çağrılır
- Dosya kapatma, ağ bağlantıları, kilit serbest bırakma gibi işlemler için kullanın

---

## 6. Generik Konteyner

Generik'leri kullanarak tip güvenli konteynerler oluşturun.

```liva
struct Stack<T> {
    var items: [T]
}

impl<T> Stack<T> {
    // T ilk elemandan çıkarılır. Argümansız `Stack.new()` henüz T'yi
    // çözemiyor (bkz. roadmap), bu yüzden stack oluşturulurken tohumlanır.
    func new(first: T) -> Stack<T> {
        var s = Stack { items: [] }
        s.items.push(first)
        return s
    }

    func push(ref mut self, item: T) {
        self.items.push(item)
    }

    func pop(ref mut self) -> T? {
        if self.items.isEmpty {
            return nil
        }
        return self.items.pop()
    }

    func peek(ref self) -> T? {
        if self.items.isEmpty {
            return nil
        }
        return self.items[self.items.length - 1]
    }

    func size(ref self) -> i64 {
        return self.items.length
    }
}

func main() {
    var intStack = Stack.new(10)
    intStack.push(20)
    intStack.push(30)

    while let val = intStack.pop() {
        println(val)
    }
    // Output: 30 20 10

    var strStack = Stack.new("hello")
    strStack.push("world")
    println(strStack.peek() ?? "empty")  // world
}
```

> **Not (2026-07):** generik `-> T?` metodlar ve değer döndüren `pop()` artık
> çalışıyor (ikisi de codegen açığıydı, düzeltildi). Kalan tek kısıtlama:
> **argümansız** generik statik metod (`Stack.new()` parametresiz) `T`'yi
> çıkaramıyor ve sessizce hatalı derleniyor — yukarıdaki `new(first: T)` gibi
> tipe her zaman bir çıkarım kaynağı verin. `roadmap.md`'de takip ediliyor.

---

## 7. HTTP İstekleri

`http::http` modülü ile REST API'lerinden veri çekin.

```liva
import http::http
import json::json

// Basit GET
func fetchUser(id: i32) -> string {
    let url = format("https://api.example.com/users/{}", id)
    let resp = HttpRequest.get(url).send()
    if resp.is2xx() {
        return resp.text()
    }
    return ""
}

// JSON gövdeli POST
func createUser(name: string) -> bool {
    let r = HttpRequest.post("https://api.example.com/users")
        .json(format("{{\"name\":\"{}\"}}", name))
        .send()
    return r.is2xx()
}

// Ortak temel URL, kimlik doğrulama ve zaman aşımı ile yeniden kullanılabilir istemci
func fetchMultipleUsers(ids: [i32]) -> [string] {
    let client = HttpClient.withBaseUrl("https://api.example.com")
        .withHeader("Authorization", "Bearer TOKEN")
        .withTimeout(5000)
    var results: [string] = []
    for id in ids {
        let r = client.get(format("/users/{}", id))
        if r.is2xx() {
            results.push(r.text())
        }
    }
    return results
}

// JSON yanıtını ayrıştır — let'e bağlayın; json() DOM'a sahiptir
func fetchAndParse() {
    let resp = HttpRequest.get("https://api.example.com/items").send()
    if resp.is2xx() {
        let doc = resp.json()                 // JsonValue DOM'a sahiptir
        let root = doc.object()
        println(root.getString("name"))
    }
}

func main() {
    let users = fetchMultipleUsers([1, 2, 3])
    for user in users {
        println(user)
    }
}
```

**Önemli noktalar:**
- `HttpRequest` akıcı değiştirilemez bir oluşturucudur; her metot yeni bir değer döndürür
- `send()` isteği anında gerçekleştirir ve `HttpResponse` değeri döndürür (tanıtıcı yok)
- `HttpResponse` yerel kaynak tutmaz — kopyalanması, döndürülmesi ve zincire alınması güvenlidir
- `resp.json()`, **DOM'a sahip** bir `JsonValue` döndürür; her zaman `let`'e bağlayın
- `HttpClient`, yeniden kullanılabilir varsayılanları (temel URL, başlıklar, zaman aşımı) paketler

---

## 8. Kanal Tabanlı Üretici/Tüketici

Görevler arasında iletişim için kanalları kullanın.

```liva
import std::channel
import std::task

func main() {
    let ch = channelCreate(100)
    let group = taskGroupCreate()

    // Producer
    taskGroupSpawn(group, || {
        for i in 0..10 {
            channelSend(ch, i * i)
        }
        channelClose(ch)
    })

    // Consumer
    taskGroupSpawn(group, || {
        var sum = 0
        for i in 0..10 {
            let val = channelRecv(ch)
            sum = sum + val
        }
        println("Sum of squares: \(sum)")
    })

    taskGroupAwaitAll(group)
}
```

**Önemli noktalar:**
- Kanallar tipli ve tamponludur
- `channelClose` artık değer gönderilmeyeceğini bildirir
- Tüm görevleri beklemek için `taskGroupAwaitAll` kullanın

---

## 9. C Kütüphaneleri ile FFI

`extern "C"` kullanarak Liva'dan C fonksiyonlarını çağırın.

```liva
extern "C" {
    func strlen(s: string) -> u64
    func strcmp(s1: string, s2: string) -> i32
    func printf(fmt: string, ...) -> i32
}

func main() {
    let s = "Hello, World!"
    let length = strlen(s)
    println("strlen: \(length)")

    let cmp = strcmp("abc", "abd")
    if cmp < 0 {
        println("abc comes before abd")
    }

    printf("C-style: %s has %d chars\n", s, length)
}
```

**Önemli noktalar:**
- `extern "C"` fonksiyonları C bağlama kurallarıyla tanımlar
- C değişken argümanları için `...` kullanın (yalnızca extern fonksiyonlarda)
- Harici kütüphaneler için `liva.toml` dosyasına `-l` bayrakları ekleyin:

```toml
[build]
extra_flags = ["-lm", "-lpthread"]
```

---

## 10. WASM'a Çapraz Derleme

Liva programlarını WebAssembly'ye derleyin.

### Kaynak Kodu

```liva
// hello_wasm.liva
func add(a: i32, b: i32) -> i32 {
    return a + b
}

func main() {
    println("Hello from WASM!")
    println(add(40, 2))
}
```

### Derleme

```bash
# Compile to WASM
livac build --target wasm32 -o hello.wasm hello_wasm.liva

# Or set target in liva.toml:
# [build]
# target = "wasm32"
```

### Diğer Çapraz Derleme Hedefleri

```bash
# Linux x86_64
livac build --target x86_64-linux-gnu

# Linux ARM64
livac build --target aarch64-linux-gnu

# macOS ARM64
livac build --target aarch64-apple-darwin
```

**Önemli noktalar:**
- Çapraz derleme için `--target <triple>` kullanın
- WASM çıktısı çalışma zamanı kütüphanesi bağlamasını atlar
- Çapraz derleme için LLVM'in AllTargets bileşeni başlatılmalıdır

---

## 11. Sınıflar ve Kalıtım

Kalıtım ve sanal dispatch ile referans tipli nesneler için sınıfları kullanın.

```liva
class Shape {
    var color: string

    init(color: string) {
        self.color = color
    }

    func area(ref self) -> f64 {
        return 0.0
    }
}

class Circle : Shape {
    var radius: f64

    init(radius: f64, color: string) {
        super.init(color)
        self.radius = radius
    }

    override func area(ref self) -> f64 {
        return 3.14159 * self.radius * self.radius
    }
}

func main() {
    let c = Circle(5.0, "kırmızı")
    println("Alan: \(c.area()), Renk: \(c.color)")
}
```

**Önemli noktalar:**
- Sınıflar heap allocation ve referans semantiği kullanır
- Üst sınıf metodlarını geçersiz kılarken `override` zorunludur
- `super.init(...)` üst sınıf yapıcısını çağırır
- `deinit` nesne kapsam dışına çıktığında otomatik çalışır

---

## 12. For Await ve Channel'larla Async

Veri akışı için channel ve async iterasyonu birleştirin.

```liva
import std::channel
import std::task

func main() {
    let ch = channelCreate(100)
    let group = taskGroupCreate()

    // Üretici
    taskGroupSpawn(group, || {
        for i in 0..10 {
            channelSend(ch, i * i)
        }
        channelClose(ch)
    })

    // Tüketici
    taskGroupSpawn(group, || {
        var total = 0
        for i in 0..10 {
            let val = channelRecv(ch)
            total = total + val
        }
        println("Toplam: \(total)")
    })

    taskGroupAwaitAll(group)
}
```

---

## 13. Crypto: Hash ve HMAC

Hashleme ve mesaj doğrulama için crypto modülünü kullanın.

```liva
import std::crypto

func main() {
    let hash = sha256("merhaba dünya")
    println("SHA-256: \(hash)")

    let md5hash = md5("merhaba dünya")
    println("MD5: \(md5hash)")

    let mac = hmacSha256("gizli-anahtar", "önemli mesaj")
    println("HMAC: \(mac)")
}
```

---

## 14. Ayrı Derleme

Dosyaları tek tek obje dosyalarına derleyip sonra linkleyin.

```bash
# Her dosyayı .o'ya derle
livac --emit-obj modul_a.liva
livac --emit-obj modul_b.liva

# Tüm obje dosyalarını linkle
livac link modul_a.o modul_b.o -o uygulama
```

**Önemli noktalar:**
- `--emit-obj` linkleme yapmadan `.o` obje dosyası üretir
- `livac link` birden fazla obje dosyasını final çalıştırılabilire linkler
- `livac build` komutu bunu otomatik olarak önbellekleme ile yapar

---

## 14. Const Generics

```liva
// Derleme zamani boyut parametreleri
func repeat<const N: i32>(value: i32) -> i32 {
    return N * value
}

let result = repeat<5>(3)   // N = 5, sonuc = 15
```

## 15. Explicit Lifetime Syntax

```liva
// Referanslarda yasam suresi anotasyonu
func longest<'a>(x: ref 'a String, y: ref 'a String) -> ref 'a String {
    if x.length() > y.length() {
        return x
    }
    return y
}
```

## 16. Generator Fonksiyonlar (Yield)

```liva
func fibonacci() {
    var a = 0
    var b = 1
    while true {
        yield a        // deger uret ve askiya al
        let tmp = a
        a = b
        b = tmp + b
    }
}
```

## 17. Enum Discriminant Degerleri

```liva
enum HttpStatus {
    case OK = 200
    case NotFound = 404
    case InternalError = 500
}
```

## 18. Generic Associated Types (GATs)

```liva
protocol LendingIterator {
    type Item<'a>
    func next(mut self) -> i32
}
```

---

## 19. Swift-Tarzı Sınıf Özellikleri

Tam sınıf araç setine ait reçeteler.

### 19.1 Statik factory ve sayaç

```liva
class Counter {
    static var total: i32

    static func make() -> Counter {
        Counter.total = Counter.total + 1
        return Counter()
    }
}
```

### 19.2 Hesaplanmış özellikler

```liva
class Temperature {
    var celsius: f64

    init(c: f64) { self.celsius = c }

    var fahrenheit: f64 {
        get { return self.celsius * 1.8 + 32.0 }
        set { self.celsius = (newValue - 32.0) / 1.8 }
    }
}
```

### 19.3 Loglama için özellik gözlemcileri

```liva
class Volume {
    var level: i32 {
        willSet {
            println("değişiyor:")
            println(self.level)
            println("yeni değer:")
            println(newValue)
        }
        didSet { println("değişti") }
    }
    init() { self.level = 0 }
}
```

### 19.4 Failable init — girdi doğrulama

```liva
class Age {
    var value: i32
    init?(v: i32) {
        if v < 0 {
            return nil
        }
        self.value = v
    }
}

let a = Age(-5)     // nil
let b = Age(30)     // Age?
```

### 19.5 Designated + convenience init aşırı yükleme

```liva
class Point {
    var x: i32
    var y: i32

    init(x: i32, y: i32) {
        self.x = x
        self.y = y
    }

    convenience init() {
        self.x = 0
        self.y = 0
    }
}

let a = Point(5, 10)   // designated
let b = Point()        // convenience
```

### 19.6 Pahalı hesaplamalar için lazy

```liva
class Report {
    var rows: i32
    lazy var summary: i32 = self.rows * self.rows * 1000

    init(rows: i32) { self.rows = rows }
}

let r = Report(500)
println(r.summary)   // ilk erişimde hesaplanır
println(r.summary)   // ikinci seferde cache'ten
```

### 19.7 Subscript tabanlı tablo

```liva
class Table {
    var base: i32
    init() { self.base = 0 }

    subscript(i: i32) -> i32 {
        get { return self.base + i }
        set { self.base = newValue - i }
    }
}

let t = Table()
let x = t[5]         // getter
t[10] = 100          // setter (newValue = 100)
```

### 19.8 `is` / `as?` ile çalışma zamanı tip kontrolü

```liva
class Shape {
    func name(ref self) -> string { return "Shape" }
}
class Circle : Shape { override func name(ref self) -> string { return "Circle" } }
class Square : Shape { override func name(ref self) -> string { return "Square" } }

func describe(s: Shape) {
    if s is Circle {
        println("daire bu")
    }
    let sq = s as? Square
    if sq != nil {
        println(sq!.name())
    }
}
```

### 19.9 Final singleton

```liva
final class Config {
    var appName: string
    init(n: string) { self.appName = n }
}
```

### 19.10 Erişim seviyeleri

```liva
open class Widget { }          // kalıtım alınabilir
public class Sealed { }        // kalıtım alınamaz
class Button : Widget { }      // OK

class Account {
    private var pin: i32        // sadece Account içinden
    fileprivate var balance: i32 // diğer çağrılardan gizli
    init() {
        self.pin = 0
        self.balance = 0
    }
}
```

### 19.11 Extension metodu

```liva
struct Point {
    var x: i32
    var y: i32
}

extension Point {
    func sum(self) -> i32 { return self.x + self.y }
}
```

---

## 20. SQLite: Prepared Statements

Parametre bağlamalı, tekrar kullanılabilir sorgular için prepared
statement kullanın. Bind indeksleri 1 tabanlı, kolon indeksleri 0
tabanlıdır.

```liva
import sqlite::sqlite

func main() {
    if let d = SqliteDB.openMemory() {
        var db = d
        db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")

        // Tek prepared INSERT'ü birden fazla satır için yeniden kullan.
        if let p = db.prepare("INSERT INTO users (name, age) VALUES (?, ?)") {
            var ins = p
            for pair in [("Alice", 30), ("Bob", 17), ("Charlie", 25)] {
                ins.bindText(1, pair.0)
                ins.bindInt(2, pair.1 as i64)
                ins.step()
                ins.reset()
            }
            ins.finalize()
        }

        // Kullanıcı girdisini ASLA SQL string'e gömmeyin —
        // `'; DROP TABLE users; --` bile bindText üzerinden geçtiğinde
        // sadece düz değer olarak saklanır.
        if let p = db.prepare("SELECT name, age FROM users WHERE age > ? ORDER BY age") {
            var q = p
            q.bindInt(1, 18 as i64)
            while q.step() {
                println("\(q.columnText(0)): \(q.columnInt(1))")
            }
            q.finalize()
        }
        db.close()
    }
}
```

**Önemli noktalar:**
- `prepare` `Stmt?` döner — SQL derlenmezse `nil`
- `step()` satır varsa `true`, ifade tamamlandığında `false`
- `reset()` ifadeyi başa sarar; bound parametreler taşınır
- `bindText` içeride `SQLITE_TRANSIENT` kullanır (SQLite kopyalar)

---

## 21. WebSocket: Gerçek Zamanlı Echo

Bir WebSocket sunucusuna bağlan, metin çerçevesi gönder ve yanıtı oku.

```liva
import websocket::websocket

func main() {
    var ws = WsClient.to("wss://echo.websocket.events")
        .keepAlive(30000)
        .connect()           // -> WebSocket (zorunlu tür)
    if !ws.isOpen() {
        println("bağlantı başarısız")
        return
    }
    if !ws.send("liva'dan merhaba") {
        println("gönderim başarısız")
        return
    }
    if let m = ws.recv() {
        println("sunucu: \(m.text())")
    }
    // ws kapsam bitişinde Drop ile otomatik kapanır
}
```

**Önemli noktalar:**
- `connect()` **zorunlu** (non-optional) `WebSocket` döner; başarısızlık için `isOpen()` kontrol edilir
- `recv()`, `WsMessage?` döner — karşı taraf kapatınca `nil`
- `WsMessage`, soket tampon belleğine bir görünümdür; `recv()`'i yeniden çağırmadan veya soketi kapatmadan önce verilerini okuyun
- URL'ler `ws://` (port 80) veya `wss://` (port 443) kullanır
- `recv()` parçalı çerçeveleri otomatik birleştirir
- `closeWith(status, sebep)` özel kapatma kodu gönderir
- `keepAlive` en az 15000 ms olmalıdır (WinHTTP); manuel ping çerçeveleri gönderilemez

---

## 22. JWT: İmzala ve Doğrula

HMAC ile imzalanan token'ları yayınla ve doğrula. Doğrulama,
HMAC'i sabit zamanda karşılaştırır.

```liva
import jwt::jwt

func main() {
    let secret = "paylasilan-gizli-anahtar"
    let payload = "{\"sub\":\"alice\",\"role\":\"admin\",\"exp\":1700000000}"

    // HS256 ile imzala
    let token = Jwt.signHS256(secret, payload)
    println(token.toString())

    // Doğrula ve payload'u geri al
    if let dogrulanmis = Jwt.verifyHS256(token.toString(), secret) {
        println("payload: \(dogrulanmis)")
    } else {
        println("token geçersiz")
    }

    // Kurcalama tespiti: yanlış secret token'ı reddeder
    if let _ = Jwt.verifyHS256(token.toString(), "yanlis-secret") {
        println("asla")
    } else {
        println("beklendiği gibi reddedildi")
    }
}
```

**Önemli noktalar:**
- HS256 SHA-256, HS512 SHA-512 kullanır
- İmza segmenti Base64URL ile kodlanır (padding yok)
- Doğrulama başarıda payload JSON'u, başarısız imzada `nil` döner
- Zamanlama saldırılarına karşı `constTimeEq` ile karşılaştırma yapılır

---

## 23. TOML: Yapılandırma Dosyası

Yapılandırma dosyasını ayrıştır ve değerleri tipli olarak oku.

```liva
import path::path
import toml::toml

func main() {
    let metin = Path.new("config.toml").read() ?? ""
    let doc = TomlDocument.parse(metin)

    if doc.isValid() {
        let host = doc.getString("server", "host") ?? "localhost"
        let port = doc.getInt("server", "port") ?? 8080 as i64
        let debug = doc.getBool("server", "debug") ?? false
        println("dinleniyor \(host):\(port) (debug=\(debug))")
    } else {
        println("geçersiz TOML")
    }
    doc.free()
}
```

`config.toml`:
```toml
[server]
host = "0.0.0.0"
port = 9000
debug = true
```

**Önemli noktalar:**
- `getString` / `getInt` / `getBool` `Optional<T>` döner — varsayılan için `??`
- `hasKey(section, key)` değer okumadan varlık kontrolü yapar
- `liva.toml` için paket yöneticisi tarafından kullanılır

---

## 24. Gzip: Sıkıştır ve Aç

İkili veriyi gzip'le turla. Encoder LZ77 + sabit Huffman bloğu;
decoder her üç deflate blok tipini de kabul eder.

```liva
import std::compress

func main() {
    let original = "hello hello hello hello hello world world world"
    let bytes: [u8] = strToBytes(original)

    // Sıkıştır (LZ77 + sabit Huffman ile RFC 1952 gzip)
    let gz: [u8] = gzipEncode(bytes)
    println("\(bytes.length) bayt -> \(gz.length) bayt")

    // Aç (stored, fixed ve dynamic Huffman bloklarını destekler)
    if let plain = gzipDecode(gz) {
        let recovered = bytesToStr(plain)
        if recovered == original {
            println("turlama tamam")
        }
    } else {
        println("açma başarısız — geçerli gzip değil")
    }
}
```

**Önemli noktalar:**
- Girdi/çıktı `[u8]` byte dizisi — `String` ↔ `[u8]` için
  `strToBytes` / `bytesToStr` kullanın
- `gzipDecode` `Optional<[u8]>` döner — bozuk girdide `nil`
- Encoder tekrar eden veride anlamlı sıkıştırma sağlar
  (1000 aynı bayt <100 bayta sıkışır)

---

## 25. Pattern Matching: HTTP Durum Kodu Sınıflandırma

Bir HTTP durum kodunu tek bir `match` içinde sınıflandırmak için range, or ve `@` binding pattern'lerini birleştirin.

```liva
func classify(status: i32) -> string {
    let label = match status {
        200 | 201 | 204 => "success"
        n @ 300..400 => "redirect(\(n))"
        404 => "not found"
        n @ 400..500 => "client error(\(n))"
        500..=599 => "server error"
        _ => "unknown"
    }
    return label
}

func main() {
    println(classify(200))   // success
    println(classify(301))   // redirect(301)
    println(classify(404))   // not found
    println(classify(422))   // client error(422)
    println(classify(503))   // server error
    println(classify(999))   // unknown
}
```

**Önemli noktalar:**
- `200 | 201 | 204` bir or-pattern'dir: üç literalden herhangi biri kolu eşleştirir
- `n @ 300..400`, durum kodunun tamamını `n`'e bağlarken eşleşmeyi `[300, 400)` aralığıyla sınırlar — range exclusive olduğundan `400` dahil değildir, bu yüzden `404` aşağıdaki kendi özel koluna düşer
- `500..=599` inclusive'dir, bu yüzden kapsanan son durum kodu `600` değil `599`'dur
- Kollar yukarıdan aşağıya denendiği için, daha spesifik bir kol (`404`), onu da eşleştirebilecek daha geniş bir range'den (`n @ 400..500`) önce gelmelidir

---

*Bu yemek kitabi, 1.0.0 surumu itibariyla Liva'daki yaygin kaliplari kapsar.*
